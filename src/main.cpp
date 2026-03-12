#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "index_html.h"

namespace {

constexpr uint16_t PWM_FREQ = 5000;
constexpr uint8_t PWM_RESOLUTION = 12;
constexpr uint16_t PWM_MAX_DUTY = (1 << PWM_RESOLUTION) - 1;

constexpr uint8_t MAX_POINTS = 16;
constexpr uint8_t MAX_PRESETS = 10;
constexpr char SCHEDULE_FILE[] = "/schedule.json";

struct KeyPoint {
  uint16_t minute;
  uint8_t value;
};

struct ChannelCurve {
  uint8_t pointCount;
  KeyPoint points[MAX_POINTS];
};

struct Preset {
  String name;
  ChannelCurve channels[LED_CHANNEL_COUNT];
};

struct SchedulerData {
  uint8_t presetCount;
  uint8_t activePreset;
  Preset presets[MAX_PRESETS];
};

SchedulerData gData{};
WebServer server(80);
String cliBuffer;

bool debugEnabled = true;
uint8_t currentOutputs[LED_CHANNEL_COUNT] = {0, 0, 0, 0, 0};
unsigned long lastPwmUpdateMs = 0;
unsigned long lastDebugMs = 0;

uint16_t clampMinute(int minute) {
  if (minute < 0) return 0;
  if (minute > 1439) return 1439;
  return static_cast<uint16_t>(minute);
}

uint8_t clampValue(int value) {
  if (value < 0) return 0;
  if (value > 255) return 255;
  return static_cast<uint8_t>(value);
}

float smoothStep(float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

void sortAndNormalizeCurve(ChannelCurve &curve) {
  if (curve.pointCount == 0) {
    curve.pointCount = 2;
    curve.points[0] = {0, 0};
    curve.points[1] = {1439, 0};
    return;
  }

  if (curve.pointCount > MAX_POINTS) {
    curve.pointCount = MAX_POINTS;
  }

  for (uint8_t i = 0; i < curve.pointCount; ++i) {
    curve.points[i].minute = clampMinute(curve.points[i].minute);
    curve.points[i].value = clampValue(curve.points[i].value);
  }

  for (uint8_t i = 0; i < curve.pointCount; ++i) {
    for (uint8_t j = i + 1; j < curve.pointCount; ++j) {
      if (curve.points[j].minute < curve.points[i].minute) {
        KeyPoint tmp = curve.points[i];
        curve.points[i] = curve.points[j];
        curve.points[j] = tmp;
      }
    }
  }

  KeyPoint dedup[MAX_POINTS];
  uint8_t dedupCount = 0;
  for (uint8_t i = 0; i < curve.pointCount; ++i) {
    if (dedupCount == 0 || dedup[dedupCount - 1].minute != curve.points[i].minute) {
      dedup[dedupCount++] = curve.points[i];
    } else {
      dedup[dedupCount - 1].value = curve.points[i].value;
    }
  }

  if (dedupCount == 1) {
    dedup[1] = {1439, dedup[0].value};
    dedupCount = 2;
  }

  curve.pointCount = dedupCount;
  for (uint8_t i = 0; i < dedupCount; ++i) {
    curve.points[i] = dedup[i];
  }
}

void fillDefaultPreset(Preset &preset, const String &name) {
  preset.name = name;

  const uint8_t dayShape[LED_CHANNEL_COUNT][4] = {
      {10, 120, 220, 20},
      {5, 90, 180, 10},
      {0, 60, 140, 0},
      {0, 70, 170, 0},
      {0, 35, 90, 0},
  };

  const uint16_t times[4] = {0, 480, 1020, 1439};

  for (int ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    preset.channels[ch].pointCount = 4;
    for (int i = 0; i < 4; ++i) {
      preset.channels[ch].points[i] = {times[i], dayShape[ch][i]};
    }
    sortAndNormalizeCurve(preset.channels[ch]);
  }
}

void initDefaultData() {
  gData.presetCount = 1;
  gData.activePreset = 0;
  fillDefaultPreset(gData.presets[0], "Default Reef Day");
}

bool saveSchedulerData() {
  DynamicJsonDocument doc(28672);

  doc["activePreset"] = gData.activePreset;
  JsonArray presets = doc.createNestedArray("presets");

  for (uint8_t p = 0; p < gData.presetCount; ++p) {
    JsonObject jp = presets.createNestedObject();
    jp["name"] = gData.presets[p].name;
    JsonArray channels = jp.createNestedArray("channels");

    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
      JsonArray points = channels.createNestedArray();
      const ChannelCurve &curve = gData.presets[p].channels[ch];
      for (uint8_t i = 0; i < curve.pointCount; ++i) {
        JsonObject point = points.createNestedObject();
        point["minute"] = curve.points[i].minute;
        point["value"] = curve.points[i].value;
      }
    }
  }

  File f = LittleFS.open(SCHEDULE_FILE, FILE_WRITE);
  if (!f) {
    Serial.println("[ERROR] Kon schedule file niet openen voor schrijven.");
    return false;
  }

  if (serializeJsonPretty(doc, f) == 0) {
    Serial.println("[ERROR] Kon schedule JSON niet schrijven.");
    f.close();
    return false;
  }

  f.close();
  return true;
}

bool loadSchedulerData() {
  if (!LittleFS.exists(SCHEDULE_FILE)) {
    Serial.println("[INFO] Geen schedule bestand, defaults worden aangemaakt.");
    initDefaultData();
    return saveSchedulerData();
  }

  File f = LittleFS.open(SCHEDULE_FILE, FILE_READ);
  if (!f) {
    Serial.println("[WARN] Schedule bestand kon niet gelezen worden. Defaults geladen.");
    initDefaultData();
    return false;
  }

  DynamicJsonDocument doc(28672);
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err) {
    Serial.printf("[WARN] JSON parse error: %s. Defaults geladen.\n", err.c_str());
    initDefaultData();
    return false;
  }

  JsonArray presets = doc["presets"].as<JsonArray>();
  if (presets.isNull() || presets.size() == 0) {
    initDefaultData();
    return false;
  }

  gData.presetCount = min<uint8_t>(presets.size(), MAX_PRESETS);
  gData.activePreset = min<uint8_t>(doc["activePreset"] | 0, gData.presetCount - 1);

  for (uint8_t p = 0; p < gData.presetCount; ++p) {
    JsonObject jp = presets[p].as<JsonObject>();
    const char *presetName = jp["name"] | "Preset";
    gData.presets[p].name = String(presetName);

    JsonArray channels = jp["channels"].as<JsonArray>();
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
      ChannelCurve &curve = gData.presets[p].channels[ch];
      curve.pointCount = 0;

      if (!channels.isNull() && ch < channels.size()) {
        JsonArray points = channels[ch].as<JsonArray>();
        for (JsonObject point : points) {
          if (curve.pointCount >= MAX_POINTS) break;
          curve.points[curve.pointCount].minute = clampMinute(point["minute"] | 0);
          curve.points[curve.pointCount].value = clampValue(point["value"] | 0);
          curve.pointCount++;
        }
      }
      sortAndNormalizeCurve(curve);
    }
  }

  return true;
}

float getMinuteOfDay() {
  time_t now = time(nullptr);
  if (now < 100000) {
    return (millis() / 1000.0f) / 60.0f;
  }

  struct tm localTime;
  localtime_r(&now, &localTime);
  return localTime.tm_hour * 60.0f + localTime.tm_min + (localTime.tm_sec / 60.0f);
}

uint8_t evaluateCurve(const ChannelCurve &curve, float minuteOfDay) {
  if (curve.pointCount == 0) return 0;
  if (curve.pointCount == 1) return curve.points[0].value;

  float minute = minuteOfDay;
  while (minute < 0.0f) minute += 1440.0f;
  while (minute >= 1440.0f) minute -= 1440.0f;

  const KeyPoint *a = nullptr;
  const KeyPoint *b = nullptr;
  float segmentStart = 0.0f;
  float segmentEnd = 0.0f;

  for (uint8_t i = 0; i < curve.pointCount - 1; ++i) {
    if (minute >= curve.points[i].minute && minute <= curve.points[i + 1].minute) {
      a = &curve.points[i];
      b = &curve.points[i + 1];
      segmentStart = a->minute;
      segmentEnd = b->minute;
      break;
    }
  }

  if (!a || !b) {
    a = &curve.points[curve.pointCount - 1];
    b = &curve.points[0];
    segmentStart = a->minute;
    segmentEnd = b->minute + 1440.0f;
    if (minute < curve.points[0].minute) {
      minute += 1440.0f;
    }
  }

  float span = segmentEnd - segmentStart;
  float t = (span > 0.0f) ? ((minute - segmentStart) / span) : 0.0f;
  float eased = smoothStep(t);
  float value = a->value + (b->value - a->value) * eased;

  return clampValue(static_cast<int>(roundf(value)));
}

void writePwm(uint8_t channel, uint8_t value) {
  uint32_t duty = map(value, 0, 255, 0, PWM_MAX_DUTY);
  ledcWrite(channel, duty);
}

void updateOutputs() {
  if (gData.presetCount == 0) return;

  float minute = getMinuteOfDay();
  const Preset &active = gData.presets[gData.activePreset];

  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    uint8_t out = evaluateCurve(active.channels[ch], minute);
    currentOutputs[ch] = out;
    writePwm(ch, out);
  }
}

void printStatusToSerial() {
  float minute = getMinuteOfDay();
  int hour = static_cast<int>(minute) / 60;
  int min = static_cast<int>(minute) % 60;

  Serial.printf("[STAT] Time %02d:%02d | Preset %u: %s | Out:", hour, min, gData.activePreset,
                gData.presets[gData.activePreset].name.c_str());
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    Serial.printf(" ch%u=%u", ch + 1, currentOutputs[ch]);
  }
  Serial.println();
}

String stateJson() {
  DynamicJsonDocument doc(32768);
  doc["activePreset"] = gData.activePreset;
  doc["nowMinute"] = getMinuteOfDay();

  JsonArray out = doc.createNestedArray("outputs");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    out.add(currentOutputs[ch]);
  }

  JsonArray presets = doc.createNestedArray("presets");
  for (uint8_t p = 0; p < gData.presetCount; ++p) {
    JsonObject jp = presets.createNestedObject();
    jp["name"] = gData.presets[p].name;
    JsonArray channels = jp.createNestedArray("channels");
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
      JsonArray points = channels.createNestedArray();
      const ChannelCurve &curve = gData.presets[p].channels[ch];
      for (uint8_t i = 0; i < curve.pointCount; ++i) {
        JsonObject point = points.createNestedObject();
        point["minute"] = curve.points[i].minute;
        point["value"] = curve.points[i].value;
      }
    }
  }

  String payload;
  serializeJson(doc, payload);
  return payload;
}

void sendJson(int code, const JsonDocument &doc) {
  String payload;
  serializeJson(doc, payload);
  server.send(code, "application/json", payload);
}

void handleGetState() {
  server.send(200, "application/json", stateJson());
}

bool parsePresetFromJson(JsonVariantConst root, Preset &outPreset) {
  const char *name = root["name"] | "Preset";
  outPreset.name = String(name);

  JsonArrayConst channels = root["channels"].as<JsonArrayConst>();
  if (channels.isNull() || channels.size() != LED_CHANNEL_COUNT) {
    return false;
  }

  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    JsonArrayConst points = channels[ch].as<JsonArrayConst>();
    if (points.isNull() || points.size() == 0) {
      return false;
    }

    ChannelCurve &curve = outPreset.channels[ch];
    curve.pointCount = 0;

    for (JsonObjectConst point : points) {
      if (curve.pointCount >= MAX_POINTS) break;
      curve.points[curve.pointCount].minute = clampMinute(point["minute"] | 0);
      curve.points[curve.pointCount].value = clampValue(point["value"] | 0);
      curve.pointCount++;
    }
    sortAndNormalizeCurve(curve);
  }

  return true;
}

void handlePresetUpsert() {
  DynamicJsonDocument body(28672);
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid json";
    sendJson(400, resp);
    return;
  }

  Preset p;
  if (!parsePresetFromJson(body.as<JsonVariantConst>(), p)) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid preset payload";
    sendJson(400, resp);
    return;
  }

  int index = body["index"] | -1;
  bool created = false;

  if (index < 0) {
    if (gData.presetCount >= MAX_PRESETS) {
      DynamicJsonDocument resp(256);
      resp["ok"] = false;
      resp["error"] = "max presets reached";
      sendJson(400, resp);
      return;
    }
    gData.presets[gData.presetCount] = p;
    gData.activePreset = gData.presetCount;
    gData.presetCount++;
    created = true;
  } else {
    if (index >= gData.presetCount) {
      DynamicJsonDocument resp(256);
      resp["ok"] = false;
      resp["error"] = "preset index out of range";
      sendJson(400, resp);
      return;
    }
    gData.presets[index] = p;
    gData.activePreset = index;
  }

  saveSchedulerData();

  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  resp["created"] = created;
  resp["activePreset"] = gData.activePreset;
  sendJson(200, resp);
}

void handlePresetSelect() {
  DynamicJsonDocument body(512);
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid json";
    sendJson(400, resp);
    return;
  }

  int index = body["index"] | 0;
  if (index < 0 || index >= gData.presetCount) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "preset index out of range";
    sendJson(400, resp);
    return;
  }

  gData.activePreset = index;
  saveSchedulerData();

  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  resp["activePreset"] = gData.activePreset;
  sendJson(200, resp);
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/state", HTTP_GET, handleGetState);
  server.on("/api/preset/upsert", HTTP_POST, handlePresetUpsert);
  server.on("/api/preset/select", HTTP_POST, handlePresetSelect);

  server.onNotFound([]() {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "not found";
    sendJson(404, resp);
  });

  server.begin();
  Serial.println("[HTTP] Webserver gestart op poort 80.");
}

void setupPwm() {
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    ledcSetup(ch, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(LED_PINS[ch], ch);
    writePwm(ch, 0);
  }
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("[WIFI] Verbinden met %s", WIFI_SSID);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 80) {
    delay(250);
    Serial.print(".");
    retry++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] Verbonden. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WIFI] Niet verbonden. Webinterface/OTA niet beschikbaar.");
  }
}

void setupTimeSync() {
  setenv("TZ", TZ_INFO, 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "europe.pool.ntp.org");
  Serial.println("[NTP] Tijd synchronisatie gestart.");
}

void setupOta() {
  ArduinoOTA.setHostname(DEVICE_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() { Serial.println("[OTA] Start update"); });
  ArduinoOTA.onEnd([]() { Serial.println("[OTA] Update klaar"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] %u%%\r", (progress * 100U) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Fout %u\n", error);
  });

  ArduinoOTA.begin();
  Serial.println("[OTA] OTA service actief.");
}

void printCliHelp() {
  Serial.println("CLI commands:");
  Serial.println("  help                - toon commando overzicht");
  Serial.println("  status              - toon live status");
  Serial.println("  list                - toon presets");
  Serial.println("  select <i>          - activeer preset index");
  Serial.println("  debug on|off        - periodieke debug output aan/uit");
  Serial.println("  save                - forceer opslag van schedule");
  Serial.println("  wifi                - toon wifi status");
}

void handleCliCommand(String cmd) {
  cmd.trim();
  if (cmd.isEmpty()) return;

  if (cmd == "help") {
    printCliHelp();
  } else if (cmd == "status") {
    printStatusToSerial();
  } else if (cmd == "list") {
    Serial.printf("[CLI] Presets (%u):\n", gData.presetCount);
    for (uint8_t i = 0; i < gData.presetCount; ++i) {
      Serial.printf("  %u: %s%s\n", i, gData.presets[i].name.c_str(), (i == gData.activePreset ? " [active]" : ""));
    }
  } else if (cmd.startsWith("select ")) {
    int index = cmd.substring(7).toInt();
    if (index >= 0 && index < gData.presetCount) {
      gData.activePreset = index;
      saveSchedulerData();
      Serial.printf("[CLI] Preset %d geactiveerd.\n", index);
    } else {
      Serial.println("[CLI] Ongeldige preset index.");
    }
  } else if (cmd == "debug on") {
    debugEnabled = true;
    Serial.println("[CLI] Debug output ingeschakeld.");
  } else if (cmd == "debug off") {
    debugEnabled = false;
    Serial.println("[CLI] Debug output uitgeschakeld.");
  } else if (cmd == "save") {
    Serial.printf("[CLI] Save %s\n", saveSchedulerData() ? "ok" : "failed");
  } else if (cmd == "wifi") {
    Serial.printf("[CLI] WiFi status: %d, IP: %s\n", WiFi.status(), WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[CLI] Onbekend commando, gebruik help.");
  }
}

void handleSerialCli() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (!cliBuffer.isEmpty()) {
        handleCliCommand(cliBuffer);
        cliBuffer = "";
      }
    } else if (isPrintable(static_cast<unsigned char>(c))) {
      cliBuffer += c;
      if (cliBuffer.length() > 120) {
        cliBuffer = cliBuffer.substring(0, 120);
      }
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[AquaLed] Boot gestart.");

  setupPwm();

  if (!LittleFS.begin(true)) {
    Serial.println("[FS] LittleFS kon niet starten.");
  }

  loadSchedulerData();

  connectWifi();
  if (WiFi.status() == WL_CONNECTED) {
    setupTimeSync();
    setupOta();
    setupWebServer();
  }

  printCliHelp();
  Serial.println("[AquaLed] Klaar.");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
    server.handleClient();
  }

  handleSerialCli();

  const unsigned long now = millis();
  if (now - lastPwmUpdateMs >= 250) {
    lastPwmUpdateMs = now;
    updateOutputs();
  }

  if (debugEnabled && now - lastDebugMs >= 5000) {
    lastDebugMs = now;
    printStatusToSerial();
  }
}
