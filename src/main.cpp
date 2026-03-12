#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "index_html.h"
#include "settings_html.h"

namespace {

constexpr uint16_t PWM_FREQ = 5000;
constexpr uint8_t PWM_RESOLUTION = 12;
constexpr uint16_t PWM_MAX_DUTY = (1 << PWM_RESOLUTION) - 1;

constexpr uint8_t MAX_POINTS = 16;
constexpr uint8_t MAX_PRESETS = 10;
constexpr char SCHEDULE_FILE[] = "/schedule.json";
constexpr char WIFI_FILE[] = "/wifi.json";

constexpr unsigned long PWM_UPDATE_MS = 50;
constexpr unsigned long DEBUG_PRINT_MS = 5000;
constexpr unsigned long WIFI_RETRY_MS = 20000;
constexpr byte DNS_PORT = 53;

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
  String channelColors[LED_CHANNEL_COUNT];
};

struct WifiConfigData {
  String ssid;
  String password;
  String otaPassword;
  String timezone;
};

SchedulerData gData{};
WifiConfigData gWifiConfig{};
WebServer server(80);
DNSServer dnsServer;
String cliBuffer;

bool debugEnabled = true;
bool fsReady = false;
bool apModeActive = false;
bool ntpConfigured = false;
bool otaConfigured = false;
bool manualTimeActive = false;
bool simulationActive = false;
bool previewActive = false;
bool otaInProgress = false;
bool debugWasEnabledBeforeOta = false;

uint16_t manualTimeBaseMinute = 0;
unsigned long manualTimeSetMs = 0;
uint16_t simulationStartMinute = 0;
unsigned long simulationStartMs = 0;
uint16_t simulationDaySeconds = 120;
uint16_t previewMinute = 0;

uint8_t currentOutputs[LED_CHANNEL_COUNT] = {0, 0, 0, 0, 0};
float smoothOutputs[LED_CHANNEL_COUNT] = {0};
unsigned long lastPwmUpdateMs = 0;
unsigned long lastDebugMs = 0;
unsigned long lastWifiRetryMs = 0;

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

uint16_t clampSimulationSeconds(int seconds) {
  if (seconds < 5) return 5;
  if (seconds > 3600) return 3600;
  return static_cast<uint16_t>(seconds);
}

bool isPlaceholderSsid(const String &ssid) {
  return ssid.isEmpty() || ssid == "YOUR_WIFI_SSID";
}

bool hasWifiCredentials(const WifiConfigData &cfg) {
  return !isPlaceholderSsid(cfg.ssid);
}

bool wifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool ntpTimeValid() {
  return time(nullptr) > 100000;
}

String apSsid() {
  uint64_t mac = ESP.getEfuseMac();
  uint16_t suffix = static_cast<uint16_t>(mac & 0xFFFF);
  char buf[24];
  snprintf(buf, sizeof(buf), "AquaLed-Setup-%04X", suffix);
  return String(buf);
}

void setManualTime(uint8_t hour, uint8_t minute) {
  manualTimeBaseMinute = static_cast<uint16_t>(hour * 60 + minute);
  manualTimeSetMs = millis();
  manualTimeActive = true;
  Serial.printf("[TIME] Handmatige tijd gezet op %02u:%02u\n", hour, minute);
}

void sortAndNormalizeCurve(ChannelCurve &curve) {
  if (curve.pointCount == 0) {
    curve.pointCount = 2;
    curve.points[0] = {0, 0};
    curve.points[1] = {1439, 0};
    return;
  }

  if (curve.pointCount > MAX_POINTS) curve.pointCount = MAX_POINTS;

  for (uint8_t i = 0; i < curve.pointCount; ++i) {
    curve.points[i].minute = clampMinute(curve.points[i].minute);
    curve.points[i].value = clampValue(curve.points[i].value);
  }

  for (uint8_t i = 0; i < curve.pointCount; ++i) {
    for (uint8_t j = i + 1; j < curve.pointCount; ++j) {
      if (curve.points[j].minute < curve.points[i].minute) {
        KeyPoint t = curve.points[i];
        curve.points[i] = curve.points[j];
        curve.points[j] = t;
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
  for (uint8_t i = 0; i < dedupCount; ++i) curve.points[i] = dedup[i];
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

constexpr const char *DEFAULT_COLORS[LED_CHANNEL_COUNT] = {
  "#1f7a8c", "#2d936c", "#8f6c4e", "#ba5a31", "#7b4fa3"
};

void initDefaultData() {
  gData.presetCount = 1;
  gData.activePreset = 0;
  fillDefaultPreset(gData.presets[0], "Default Reef Day");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
    gData.channelColors[ch] = String(DEFAULT_COLORS[ch]);
}

bool saveSchedulerData() {
  if (!fsReady) return false;

  DynamicJsonDocument doc(28672);
  doc["activePreset"] = gData.activePreset;
  doc["simulationDaySeconds"] = simulationDaySeconds;

  JsonArray jColors = doc.createNestedArray("channelColors");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
    jColors.add(gData.channelColors[ch]);

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
  if (!f) return false;
  bool ok = serializeJsonPretty(doc, f) > 0;
  f.close();
  return ok;
}

bool loadSchedulerData() {
  if (!fsReady) {
    initDefaultData();
    return false;
  }

  if (!LittleFS.exists(SCHEDULE_FILE)) {
    initDefaultData();
    return saveSchedulerData();
  }

  File f = LittleFS.open(SCHEDULE_FILE, FILE_READ);
  if (!f) {
    initDefaultData();
    return false;
  }

  DynamicJsonDocument doc(28672);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
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
  simulationDaySeconds = clampSimulationSeconds(doc["simulationDaySeconds"] | simulationDaySeconds);

  JsonArray jColors = doc["channelColors"].as<JsonArray>();
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    if (!jColors.isNull() && ch < jColors.size())
      gData.channelColors[ch] = String((const char *)(jColors[ch] | DEFAULT_COLORS[ch]));
    else
      gData.channelColors[ch] = String(DEFAULT_COLORS[ch]);
  }

  for (uint8_t p = 0; p < gData.presetCount; ++p) {
    JsonObject jp = presets[p].as<JsonObject>();
    gData.presets[p].name = String((const char *)(jp["name"] | "Preset"));

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

bool saveWifiConfig(const WifiConfigData &cfg) {
  if (!fsReady) return false;

  DynamicJsonDocument doc(1024);
  doc["ssid"] = cfg.ssid;
  doc["password"] = cfg.password;
  doc["otaPassword"] = cfg.otaPassword;
  doc["timezone"] = cfg.timezone;

  File f = LittleFS.open(WIFI_FILE, FILE_WRITE);
  if (!f) return false;
  bool ok = serializeJsonPretty(doc, f) > 0;
  f.close();
  return ok;
}

void loadWifiConfig(WifiConfigData &cfg) {
  cfg.ssid = "";
  cfg.password = "";
  cfg.otaPassword = String(OTA_PASSWORD);
  cfg.timezone = String(TZ_INFO);

  if (fsReady && LittleFS.exists(WIFI_FILE)) {
    File f = LittleFS.open(WIFI_FILE, FILE_READ);
    if (f) {
      DynamicJsonDocument doc(1024);
      DeserializationError err = deserializeJson(doc, f);
      f.close();
      if (!err) {
        cfg.ssid = String((const char *)(doc["ssid"] | ""));
        cfg.password = String((const char *)(doc["password"] | ""));
        cfg.otaPassword = String((const char *)(doc["otaPassword"] | OTA_PASSWORD));
        cfg.timezone = String((const char *)(doc["timezone"] | TZ_INFO));
      }
    }
  }

  if (!hasWifiCredentials(cfg)) {
    cfg.ssid = String(WIFI_SSID);
    cfg.password = String(WIFI_PASSWORD);
    if (isPlaceholderSsid(cfg.ssid)) {
      cfg.ssid = "";
      cfg.password = "";
    }
  }
}

void startConfigAp() {
  if (apModeActive) return;

  WiFi.mode(WIFI_AP_STA);
  const String ssid = apSsid();
  if (!WiFi.softAP(ssid.c_str())) {
    Serial.println("[AP] SoftAP start mislukt.");
    return;
  }

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  apModeActive = true;

  Serial.printf("[AP] Setup AP actief: %s\n", ssid.c_str());
  Serial.printf("[AP] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
}

void stopConfigAp() {
  if (!apModeActive) return;
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  apModeActive = false;
  Serial.println("[AP] Setup AP gestopt.");
}

bool connectWifiStation(uint16_t retryCycles = 80) {
  if (!hasWifiCredentials(gWifiConfig)) {
    Serial.println("[WIFI] Geen credentials.");
    return false;
  }

  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.mode(apModeActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(gWifiConfig.ssid.c_str(), gWifiConfig.password.c_str());

  Serial.printf("[WIFI] Verbinden met %s", gWifiConfig.ssid.c_str());
  for (uint16_t i = 0; i < retryCycles && WiFi.status() != WL_CONNECTED; ++i) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (wifiConnected()) {
    Serial.printf("[WIFI] Verbonden. IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  WiFi.disconnect(true);
  if (apModeActive) WiFi.mode(WIFI_AP);
  Serial.println("[WIFI] Verbinden mislukt.");
  return false;
}

void setupTimeSync() {
  if (ntpConfigured) return;
  setenv("TZ", gWifiConfig.timezone.c_str(), 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "europe.pool.ntp.org");
  ntpConfigured = true;
  Serial.println("[NTP] Tijd sync gestart.");
}

const char *otaErrorString(ota_error_t err) {
  switch (err) {
    case OTA_AUTH_ERROR:    return "Auth fout (wachtwoord?)";
    case OTA_BEGIN_ERROR:   return "Begin fout (geen ruimte?)";
    case OTA_CONNECT_ERROR: return "Verbinding mislukt";
    case OTA_RECEIVE_ERROR: return "Ontvangst fout";
    case OTA_END_ERROR:     return "Einde fout";
    default:                return "Onbekend";
  }
}

void setupOta() {
  ArduinoOTA.end();

  ArduinoOTA.setHostname(DEVICE_HOSTNAME);
  ArduinoOTA.setPassword(gWifiConfig.otaPassword.c_str());
  ArduinoOTA.setTimeout(120000);
  ArduinoOTA.setMdnsEnabled(true);
  ArduinoOTA.setRebootOnSuccess(true);
  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    debugWasEnabledBeforeOta = debugEnabled;
    debugEnabled = false;
    server.stop();
    if (apModeActive) dnsServer.stop();
    WiFi.setSleep(false);
    Serial.println("[OTA] Start upload...");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned int lastPercent = 999;
    if (progress == 0) lastPercent = 0;
    const unsigned int percent = total > 0 ? (progress * 100U) / total : 0U;
    if (percent >= lastPercent + 5U || percent == 100U) {
      lastPercent = percent;
      Serial.printf("[OTA] %u%%\n", percent);
    }
    yield();
  });
  ArduinoOTA.onEnd([]() {
    otaInProgress = false;
    debugEnabled = debugWasEnabledBeforeOta;
    Serial.println("[OTA] Klaar, herstart...");
  });
  ArduinoOTA.onError([](ota_error_t err) {
    otaInProgress = false;
    otaConfigured = false;
    debugEnabled = debugWasEnabledBeforeOta;
    Serial.printf("[OTA] Fout %u: %s\n", err, otaErrorString(err));
    delay(500);
    server.begin();
    if (apModeActive) dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  });
  ArduinoOTA.begin();
  otaConfigured = true;
  Serial.printf("[OTA] Actief. Host: %s, IP: %s\n", DEVICE_HOSTNAME, WiFi.localIP().toString().c_str());
}

void activateNetworkServicesIfConnected() {
  if (!wifiConnected()) return;
  setupTimeSync();
  setupOta();
}

float getBaseMinuteOfDay() {
  if (ntpTimeValid()) {
    time_t now = time(nullptr);
    struct tm localTime;
    localtime_r(&now, &localTime);
    return localTime.tm_hour * 60.0f + localTime.tm_min + (localTime.tm_sec / 60.0f);
  }

  if (manualTimeActive) {
    float minute = manualTimeBaseMinute + (millis() - manualTimeSetMs) / 60000.0f;
    while (minute >= 1440.0f) minute -= 1440.0f;
    return minute;
  }

  float minute = (millis() / 1000.0f) / 60.0f;
  while (minute >= 1440.0f) minute -= 1440.0f;
  return minute;
}

float getSimulatedMinuteOfDay() {
  if (!simulationActive) return getBaseMinuteOfDay();

  const unsigned long elapsedMs = millis() - simulationStartMs;
  const float dayMs = static_cast<float>(simulationDaySeconds) * 1000.0f;
  float minute = simulationStartMinute + (elapsedMs / dayMs) * 1440.0f;
  while (minute >= 1440.0f) minute -= 1440.0f;
  while (minute < 0.0f) minute += 1440.0f;
  return minute;
}

float getMinuteOfDay() {
  if (previewActive) return static_cast<float>(previewMinute);
  return getSimulatedMinuteOfDay();
}

String currentDateTimeText() {
  if (ntpTimeValid()) {
    time_t now = time(nullptr);
    struct tm localTime;
    localtime_r(&now, &localTime);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &localTime);
    return String(buf);
  }

  const float minute = getMinuteOfDay();
  const int hh = static_cast<int>(minute) / 60;
  const int mm = static_cast<int>(minute) % 60;
  char buf[32];
  snprintf(buf, sizeof(buf), "onbekend %02d:%02d", hh, mm);
  return String(buf);
}

void setSimulation(bool enabled, int daySeconds) {
  const uint16_t clampedSeconds = clampSimulationSeconds(daySeconds);
  simulationDaySeconds = clampedSeconds;

  if (enabled) {
    if (!simulationActive) {
      simulationStartMinute = static_cast<uint16_t>(roundf(getBaseMinuteOfDay()));
      simulationStartMs = millis();
    } else {
      simulationStartMinute = static_cast<uint16_t>(roundf(getSimulatedMinuteOfDay()));
      simulationStartMs = millis();
    }
    simulationActive = true;
    Serial.printf("[SIM] Aan: 1 dag in %u sec, start=%u min\n", simulationDaySeconds, simulationStartMinute);
  } else {
    const uint16_t frozenMinute = static_cast<uint16_t>(roundf(getSimulatedMinuteOfDay()));
    simulationActive = false;
    manualTimeBaseMinute = frozenMinute;
    manualTimeSetMs = millis();
    manualTimeActive = true;
    Serial.println("[SIM] Uit: huidige simulatietijd vastgezet als handmatige tijd.");
  }

  saveSchedulerData();
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
    if (minute < curve.points[0].minute) minute += 1440.0f;
  }

  float span = segmentEnd - segmentStart;
  float t = (span > 0.0f) ? ((minute - segmentStart) / span) : 0.0f;
  float value = a->value + (b->value - a->value) * smoothStep(t);
  return clampValue(static_cast<int>(roundf(value)));
}

void writePwm(uint8_t channel, uint8_t value) {
  uint32_t duty = map(value, 0, 255, 0, PWM_MAX_DUTY);
  ledcWrite(channel, duty);
}

void updateOutputs() {
  constexpr float MAX_STEP = 1.0f;

  if (gData.presetCount == 0) return;

  const Preset &active = gData.presets[gData.activePreset];
  float minute = getMinuteOfDay();

  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    float target = static_cast<float>(evaluateCurve(active.channels[ch], minute));
    if (previewActive) {
      smoothOutputs[ch] = target;
    } else {
      float diff = target - smoothOutputs[ch];
      if (fabsf(diff) <= MAX_STEP) {
        smoothOutputs[ch] = target;
      } else {
        smoothOutputs[ch] += (diff > 0.0f ? MAX_STEP : -MAX_STEP);
      }
    }
    uint8_t out = static_cast<uint8_t>(roundf(smoothOutputs[ch]));
    if (out != currentOutputs[ch]) {
      currentOutputs[ch] = out;
      writePwm(ch, out);
    }
  }
}

void printStatusToSerial() {
  float minute = getMinuteOfDay();
  int hh = static_cast<int>(minute) / 60;
  int mm = static_cast<int>(minute) % 60;

  Serial.printf("[STAT] %02d:%02d | Preset %u: %s%s | Out:", hh, mm, gData.activePreset,
                gData.presets[gData.activePreset].name.c_str(),
                previewActive ? " [PREVIEW]" : "");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    Serial.printf(" ch%u=%u%%", ch + 1, (currentOutputs[ch] * 100 + 127) / 255);
  }
  Serial.println();
}

String stateJson() {
  DynamicJsonDocument doc(32768);
  doc["activePreset"] = gData.activePreset;
  doc["nowMinute"] = getMinuteOfDay();
  doc["wifiConnected"] = wifiConnected();
  doc["ssid"] = wifiConnected() ? WiFi.SSID() : gWifiConfig.ssid;
  doc["stationIp"] = wifiConnected() ? WiFi.localIP().toString() : "0.0.0.0";
  doc["apMode"] = apModeActive;
  doc["apIp"] = apModeActive ? WiFi.softAPIP().toString() : "0.0.0.0";
  doc["ntpSynced"] = ntpTimeValid();
  doc["manualTime"] = manualTimeActive;
  doc["simulationActive"] = simulationActive;
  doc["simulationDaySeconds"] = simulationDaySeconds;
  doc["previewActive"] = previewActive;
  doc["dateTime"] = currentDateTimeText();
  doc["otaPassword"] = gWifiConfig.otaPassword;
  doc["timezone"] = gWifiConfig.timezone;

  JsonArray jColors = doc.createNestedArray("channelColors");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
    jColors.add(gData.channelColors[ch]);

  JsonArray out = doc.createNestedArray("outputs");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) out.add(currentOutputs[ch]);

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

void handleGetStateLight() {
  DynamicJsonDocument doc(512);
  doc["nowMinute"] = getMinuteOfDay();
  doc["dateTime"] = currentDateTimeText();
  doc["simulationActive"] = simulationActive;
  doc["previewActive"] = previewActive;
  JsonArray out = doc.createNestedArray("outputs");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) out.add(currentOutputs[ch]);
  sendJson(200, doc);
}

bool parsePresetFromJson(JsonVariantConst root, Preset &outPreset) {
  outPreset.name = String((const char *)(root["name"] | "Preset"));

  JsonArrayConst channels = root["channels"].as<JsonArrayConst>();
  if (channels.isNull() || channels.size() != LED_CHANNEL_COUNT) return false;

  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    JsonArrayConst points = channels[ch].as<JsonArrayConst>();
    if (points.isNull() || points.size() == 0) return false;

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
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid json";
    return sendJson(400, resp);
  }

  Preset p;
  if (!parsePresetFromJson(body.as<JsonVariantConst>(), p)) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid preset payload";
    return sendJson(400, resp);
  }

  int index = body["index"] | -1;
  bool created = false;

  if (index < 0) {
    if (gData.presetCount >= MAX_PRESETS) {
      DynamicJsonDocument resp(256);
      resp["ok"] = false;
      resp["error"] = "max presets reached";
      return sendJson(400, resp);
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
      return sendJson(400, resp);
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
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid json";
    return sendJson(400, resp);
  }

  int index = body["index"] | 0;
  if (index < 0 || index >= gData.presetCount) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "preset index out of range";
    return sendJson(400, resp);
  }

  gData.activePreset = index;
  saveSchedulerData();

  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  resp["activePreset"] = gData.activePreset;
  sendJson(200, resp);
}

void handlePresetRename() {
  DynamicJsonDocument body(512);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid json";
    return sendJson(400, resp);
  }

  int index = body["index"] | -1;
  if (index < 0 || index >= gData.presetCount) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "preset index out of range";
    return sendJson(400, resp);
  }

  String name = String((const char *)(body["name"] | ""));
  name.trim();
  if (name.isEmpty()) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "name required";
    return sendJson(400, resp);
  }

  gData.presets[index].name = name;
  saveSchedulerData();

  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  sendJson(200, resp);
}

void handlePresetDelete() {
  DynamicJsonDocument body(512);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid json";
    return sendJson(400, resp);
  }

  int index = body["index"] | -1;
  if (index < 0 || index >= gData.presetCount) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "preset index out of range";
    return sendJson(400, resp);
  }

  if (gData.presetCount <= 1) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "cannot delete last preset";
    return sendJson(400, resp);
  }

  for (int i = index; i < gData.presetCount - 1; ++i)
    gData.presets[i] = gData.presets[i + 1];
  gData.presetCount--;

  if (gData.activePreset >= gData.presetCount)
    gData.activePreset = gData.presetCount - 1;
  else if (gData.activePreset > index)
    gData.activePreset--;

  saveSchedulerData();

  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  resp["activePreset"] = gData.activePreset;
  sendJson(200, resp);
}

void handleWifiSave() {
  DynamicJsonDocument body(1024);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid json";
    return sendJson(400, resp);
  }

  String ssid = String((const char *)(body["ssid"] | ""));
  String password = body["password"].isNull()
                        ? gWifiConfig.password
                        : String((const char *)(body["password"] | ""));
  String timezone = body["timezone"].isNull()
                        ? gWifiConfig.timezone
                        : String((const char *)(body["timezone"] | TZ_INFO));
  ssid.trim();
  timezone.trim();

  String oldSsid = gWifiConfig.ssid;
  String oldPassword = gWifiConfig.password;

  if (ssid.isEmpty()) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "ssid required";
    return sendJson(400, resp);
  }

  gWifiConfig.ssid = ssid;
  if (!password.isEmpty()) {
    gWifiConfig.password = password;
  }
  if (!timezone.isEmpty()) {
    gWifiConfig.timezone = timezone;
    setenv("TZ", timezone.c_str(), 1);
    tzset();
  }
  saveWifiConfig(gWifiConfig);

  bool credentialsChanged = (ssid != oldSsid || password != oldPassword);
  bool connected = wifiConnected();
  if (credentialsChanged) {
    connected = connectWifiStation(60);
    if (connected) {
      otaConfigured = false;
      activateNetworkServicesIfConnected();
      stopConfigAp();
    } else {
      startConfigAp();
    }
  }

  DynamicJsonDocument resp(512);
  resp["ok"] = true;
  resp["connected"] = connected;
  resp["apMode"] = apModeActive;
  resp["stationIp"] = wifiConnected() ? WiFi.localIP().toString() : "0.0.0.0";
  resp["apIp"] = apModeActive ? WiFi.softAPIP().toString() : "0.0.0.0";
  sendJson(200, resp);
}

void handleTimeSet() {
  DynamicJsonDocument body(512);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid json";
    return sendJson(400, resp);
  }

  int hour = body["hour"] | -1;
  int minute = body["minute"] | -1;
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid time";
    return sendJson(400, resp);
  }

  setManualTime(static_cast<uint8_t>(hour), static_cast<uint8_t>(minute));

  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  resp["manualTime"] = true;
  resp["nowMinute"] = getMinuteOfDay();
  sendJson(200, resp);
}

void handleColorsSave() {
  DynamicJsonDocument body(1024);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid json";
    return sendJson(400, resp);
  }

  JsonArray arr = body["channelColors"].as<JsonArray>();
  if (arr.isNull() || arr.size() != LED_CHANNEL_COUNT) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "channelColors array required with 5 entries";
    return sendJson(400, resp);
  }

  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    gData.channelColors[ch] = String((const char *)(arr[ch] | DEFAULT_COLORS[ch]));
  }
  saveSchedulerData();

  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  sendJson(200, resp);
}

void handleSimulationSet() {
  DynamicJsonDocument body(512);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid json";
    return sendJson(400, resp);
  }

  bool enabled = body["enabled"] | false;
  int daySeconds = body["daySeconds"] | simulationDaySeconds;
  setSimulation(enabled, daySeconds);

  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  resp["simulationActive"] = simulationActive;
  resp["simulationDaySeconds"] = simulationDaySeconds;
  resp["nowMinute"] = getMinuteOfDay();
  sendJson(200, resp);
}

void handlePreviewSet() {
  DynamicJsonDocument body(512);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid json";
    return sendJson(400, resp);
  }

  bool enabled = body["enabled"] | false;
  if (enabled) {
    int minute = body["minute"] | 0;
    previewMinute = clampMinute(minute);
    previewActive = true;
    Serial.printf("[PREVIEW] Aan: %u min\n", previewMinute);
  } else {
    previewActive = false;
    Serial.println("[PREVIEW] Uit");
  }

  updateOutputs();

  DynamicJsonDocument resp(512);
  resp["ok"] = true;
  resp["previewActive"] = previewActive;
  resp["nowMinute"] = getMinuteOfDay();
  JsonArray out = resp.createNestedArray("outputs");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) out.add(currentOutputs[ch]);
  sendJson(200, resp);
}

void handleOtaSave() {
  DynamicJsonDocument body(512);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid json";
    return sendJson(400, resp);
  }

  String otaPassword = String((const char *)(body["otaPassword"] | ""));
  otaPassword.trim();

  if (otaPassword.isEmpty()) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "otaPassword required";
    return sendJson(400, resp);
  }

  gWifiConfig.otaPassword = otaPassword;
  saveWifiConfig(gWifiConfig);

  otaConfigured = false;
  if (wifiConnected()) setupOta();

  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  sendJson(200, resp);
}

void setupWebServer() {
  if (WiFi.getMode() == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_STA);
  }

  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/settings", HTTP_GET, []() { server.send_P(200, "text/html", SETTINGS_HTML); });

  server.on("/api/state", HTTP_GET, handleGetState);
  server.on("/api/state/light", HTTP_GET, handleGetStateLight);
  server.on("/api/preset/upsert", HTTP_POST, handlePresetUpsert);
  server.on("/api/preset/select", HTTP_POST, handlePresetSelect);
  server.on("/api/preset/rename", HTTP_POST, handlePresetRename);
  server.on("/api/preset/delete", HTTP_POST, handlePresetDelete);
  server.on("/api/wifi/save", HTTP_POST, handleWifiSave);
  server.on("/api/time/set", HTTP_POST, handleTimeSet);
  server.on("/api/simulation/set", HTTP_POST, handleSimulationSet);
  server.on("/api/preview/set", HTTP_POST, handlePreviewSet);
  server.on("/api/colors/save", HTTP_POST, handleColorsSave);
  server.on("/api/ota/save", HTTP_POST, handleOtaSave);

  server.onNotFound([]() {
    if (apModeActive) {
      server.sendHeader("Location", "/settings", true);
      server.send(302, "text/plain", "");
      return;
    }
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
    Serial.printf("[PWM] Init ch%u pin=%d\n", ch + 1, LED_PINS[ch]);
    ledcSetup(ch, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(LED_PINS[ch], ch);
    writePwm(ch, 0);
  }
  Serial.println("[PWM] Init klaar.");
}

void ensureWifiLink() {
  if (otaInProgress) return;
  const unsigned long now = millis();
  if (wifiConnected()) return;
  const unsigned long retryInterval = apModeActive ? 60000 : WIFI_RETRY_MS;
  if (now - lastWifiRetryMs < retryInterval) return;
  lastWifiRetryMs = now;

  if (!hasWifiCredentials(gWifiConfig)) {
    startConfigAp();
    return;
  }

  Serial.println("[WIFI] Reconnect poging...");
  bool connected = connectWifiStation(apModeActive ? 8 : 20);
  if (connected) {
    activateNetworkServicesIfConnected();
    stopConfigAp();
  } else {
    startConfigAp();
  }
}

void printCliHelp() {
  Serial.println("CLI commands:");
  Serial.println("  help                - toon commando overzicht");
  Serial.println("  status              - toon live status");
  Serial.println("  list                - toon presets");
  Serial.println("  select <i>          - activeer preset index");
  Serial.println("  settime HH:MM       - handmatige tijd instellen");
  Serial.println("  ap on|off           - setup AP aan/uit");
  Serial.println("  debug on|off        - periodieke debug output aan/uit");
  Serial.println("  save                - forceer opslag van schedule");
  Serial.println("  wifi                - toon wifi status");
  Serial.println("  preview HH:MM       - preview op tijdstip");
  Serial.println("  preview off         - preview uitschakelen");
  Serial.println("  preview             - toon preview status");
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
  } else if (cmd.startsWith("settime ")) {
    String t = cmd.substring(8);
    int sep = t.indexOf(':');
    if (sep > 0) {
      int hh = t.substring(0, sep).toInt();
      int mm = t.substring(sep + 1).toInt();
      if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
        setManualTime(static_cast<uint8_t>(hh), static_cast<uint8_t>(mm));
      } else {
        Serial.println("[CLI] Ongeldige tijd, gebruik HH:MM.");
      }
    } else {
      Serial.println("[CLI] Formaat: settime HH:MM");
    }
  } else if (cmd == "ap on") {
    startConfigAp();
  } else if (cmd == "ap off") {
    stopConfigAp();
  } else if (cmd == "debug on") {
    debugEnabled = true;
    Serial.println("[CLI] Debug output ingeschakeld.");
  } else if (cmd == "debug off") {
    debugEnabled = false;
    Serial.println("[CLI] Debug output uitgeschakeld.");
  } else if (cmd == "save") {
    Serial.printf("[CLI] Save %s\n", saveSchedulerData() ? "ok" : "failed");
  } else if (cmd == "wifi") {
    Serial.printf("[CLI] WiFi status: %d, STA IP: %s, AP: %s\n", WiFi.status(),
                  WiFi.localIP().toString().c_str(), apModeActive ? WiFi.softAPIP().toString().c_str() : "off");
  } else if (cmd == "preview") {
    if (previewActive) {
      int hh = previewMinute / 60;
      int mm = previewMinute % 60;
      Serial.printf("[CLI] Preview aan: %02d:%02d | Out:", hh, mm);
      for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
        Serial.printf(" ch%u=%u%%", ch + 1, (currentOutputs[ch] * 100 + 127) / 255);
      Serial.println();
    } else {
      Serial.println("[CLI] Preview uit.");
    }
  } else if (cmd.startsWith("preview ")) {
    String arg = cmd.substring(8);
    arg.trim();
    if (arg == "off") {
      previewActive = false;
      updateOutputs();
      Serial.println("[CLI] Preview uit.");
    } else {
      int sep = arg.indexOf(':');
      if (sep > 0) {
        int hh = arg.substring(0, sep).toInt();
        int mm = arg.substring(sep + 1).toInt();
        if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
          previewMinute = hh * 60 + mm;
          previewActive = true;
          updateOutputs();
          Serial.printf("[CLI] Preview aan: %02d:%02d | Out:", hh, mm);
          for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
            Serial.printf(" ch%u=%u%%", ch + 1, (currentOutputs[ch] * 100 + 127) / 255);
          Serial.println();
        } else {
          Serial.println("[CLI] Ongeldige tijd, gebruik HH:MM.");
        }
      } else {
        Serial.println("[CLI] Formaat: preview HH:MM | preview off");
      }
    }
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
      if (cliBuffer.length() > 120) cliBuffer = cliBuffer.substring(0, 120);
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[AquaLed] Boot gestart.");

  Serial.println("[BOOT] setupPwm");
  setupPwm();

  Serial.println("[BOOT] LittleFS.begin");
  fsReady = LittleFS.begin(false);
  if (!fsReady) {
    Serial.println("[FS] LittleFS mount mislukt, probeer format...");
    fsReady = LittleFS.begin(true);
    if (!fsReady) {
      Serial.println("[FS] Format+mount mislukt. Defaults gebruikt zonder persistente opslag.");
      initDefaultData();
    } else {
      Serial.println("[FS] LittleFS geformatteerd en gemount.");
      initDefaultData();
      saveSchedulerData();
    }
  } else {
    Serial.println("[FS] LittleFS actief.");
    loadSchedulerData();
  }

  Serial.println("[BOOT] loadWifiConfig");
  loadWifiConfig(gWifiConfig);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  Serial.println("[BOOT] setupWebServer");
  setupWebServer();

  Serial.println("[BOOT] connectWifiStation");
  bool connected = connectWifiStation();
  if (connected) {
    activateNetworkServicesIfConnected();
  } else {
    startConfigAp();
  }

  printCliHelp();
  Serial.println("[AquaLed] Klaar.");
}

void loop() {
  if (otaConfigured) ArduinoOTA.handle();

  if (otaInProgress) {
    delay(1);
    return;
  }

  if (!otaConfigured && wifiConnected()) setupOta();

  server.handleClient();

  if (apModeActive) dnsServer.processNextRequest();

  ensureWifiLink();
  handleSerialCli();

  const unsigned long now = millis();
  if (now - lastPwmUpdateMs >= PWM_UPDATE_MS) {
    lastPwmUpdateMs = now;
    updateOutputs();
  }

  if (debugEnabled && now - lastDebugMs >= DEBUG_PRINT_MS) {
    lastDebugMs = now;
    printStatusToSerial();
  }
}
