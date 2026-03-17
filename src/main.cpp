#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <Update.h>
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
constexpr char MQTT_FILE[] = "/mqtt.json";

constexpr unsigned long PWM_UPDATE_MS = 50;
constexpr unsigned long DEBUG_PRINT_MS = 5000;
constexpr unsigned long WIFI_RETRY_MS = 20000;
constexpr byte DNS_PORT = 53;
constexpr uint8_t ALL_CHANNELS_MASK = (1u << LED_CHANNEL_COUNT) - 1u;

struct KeyPoint {
  uint16_t minute;
  uint16_t value;
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
  float channelMaxWatts[LED_CHANNEL_COUNT];
};

struct WifiConfigData {
  String ssid;
  String password;
  String otaPassword;
  String timezone;
};

struct MqttConfigData {
  bool     enabled  = false;
  String   broker;
  uint16_t port     = 1883;
  String   username;
  String   password;
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
bool otaInProgress = false;
bool debugWasEnabledBeforeOta = false;
bool previewActive = false;
bool previewDirect = false;
uint16_t previewMinute = 0;

uint16_t manualTimeBaseMinute = 0;
unsigned long manualTimeSetMs = 0;
uint16_t simulationStartMinute = 0;
unsigned long simulationStartMs = 0;
uint16_t simulationDaySeconds = 120;

uint16_t currentOutputs[LED_CHANNEL_COUNT] = {0, 0, 0, 0, 0};
float smoothOutputs[LED_CHANNEL_COUNT] = {0};
bool masterEnabled = true;
MqttConfigData gMqttConfig{};

// Maanlicht simulatie
bool moonlightEnabled = false;
int8_t moonlightChannel = -1;   // -1 = uitgeschakeld
uint16_t moonlightIntensity = 492; // 0-4095 (~12% standaard)
bool moonlightCurrentlyActive = false;
float masterBrightness = 1.0f;  // 0.0–2.0 (0–200%), schaalsfactor over alle kanalen

// Wolken simulatie (per kanaal)
bool cloudSimEnabled = false;
bool cloudChannelEnabled[LED_CHANNEL_COUNT] = {true, true, true, true, true};
uint16_t cloudAvgDurationSec[LED_CHANNEL_COUNT] = {30, 30, 30, 30, 30};
uint16_t cloudMinDurationSec[LED_CHANNEL_COUNT] = {10, 10, 10, 10, 10};
uint16_t cloudEventsPerDay[LED_CHANNEL_COUNT] = {100, 100, 100, 100, 100};
uint8_t cloudDimPercent[LED_CHANNEL_COUNT] = {50, 50, 50, 50, 50};
uint8_t cloudCurrentDimPercent[LED_CHANNEL_COUNT] = {0, 0, 0, 0, 0};

struct CloudRuntime {
  bool active = false;
  unsigned long startMs = 0;
  unsigned long endMs = 0;
  unsigned long nextStartMs = 0;
  uint8_t peakDimPercent = 0;
};

CloudRuntime cloudRuntime[LED_CHANNEL_COUNT];

WiFiClient     mqttWifiClient;
PubSubClient   mqttClient(mqttWifiClient);
unsigned long lastMqttPublishMs   = 0;
unsigned long lastMqttReconnectMs = 0;

unsigned long lastPwmUpdateMs = 0;
unsigned long lastDebugMs = 0;
unsigned long lastWifiRetryMs = 0;

// Button state
bool buttonPressed = false;
unsigned long buttonPressedMs = 0;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 50;
constexpr unsigned long BUTTON_LONG_PRESS_MS = 3000;

uint16_t clampMinute(int minute) {
  if (minute < 0) return 0;
  if (minute > 1439) return 1439;
  return static_cast<uint16_t>(minute);
}

uint16_t clampValue(int value) {
  if (value < 0) return 0;
  if (value > 4095) return 4095;
  return static_cast<uint16_t>(value);
}

float smoothStep(float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  return t * t * (3.0f - 2.0f * t);
}

// ─── Maanfase berekening ─────────────────────────────────────────────────────
// Gebaseerd op Julian Day Number; referentie nieuwe maan: JD 2451550.1 (6 jan 2000)
constexpr double MOON_CYCLE_DAYS = 29.53059;

float calcMoonPhase() {
  time_t now = time(nullptr);
  if (now < 86400) return 0.5f;  // geen geldige tijd, geef halve maan terug
  double jd  = now / 86400.0 + 2440587.5;
  double age = fmod(jd - 2451550.1, MOON_CYCLE_DAYS);
  if (age < 0.0) age += MOON_CYCLE_DAYS;
  return static_cast<float>((1.0 - cos(2.0 * M_PI * age / MOON_CYCLE_DAYS)) / 2.0);
}

uint16_t clampSimulationSeconds(int seconds) {
  if (seconds < 5) return 5;
  if (seconds > 3600) return 3600;
  return static_cast<uint16_t>(seconds);
}

uint16_t clampCloudDurationSec(int seconds) {
  if (seconds < 1) return 1;
  if (seconds > 3600) return 3600;
  return static_cast<uint16_t>(seconds);
}

uint16_t clampCloudEventsPerDay(int count) {
  if (count < 1) return 1;
  if (count > 5000) return 5000;
  return static_cast<uint16_t>(count);
}

uint8_t clampCloudPercent(int pct) {
  if (pct < 0) return 0;
  if (pct > 100) return 100;
  return static_cast<uint8_t>(pct);
}

float random01() {
  uint32_t r = esp_random();
  if (r == 0) r = 1;
  return static_cast<float>(r) / 4294967295.0f;
}

float sampleExponential(float mean) {
  if (mean <= 0.0f) return 0.0f;
  float u = random01();
  if (u < 1e-6f) u = 1e-6f;
  return -logf(u) * mean;
}

bool cloudHasSelectedChannels() {
  return (cloudChannelsMask & ALL_CHANNELS_MASK) != 0;
}

void scheduleNextCloud(bool immediateBase = false) {
  if (!cloudSimEnabled || !cloudHasSelectedChannels()) {
    cloudNextStartMs = 0;
    return;
  }

  const float meanIntervalSec = 86400.0f / static_cast<float>(cloudEventsPerDay);
  float delaySec = sampleExponential(meanIntervalSec);
  if (delaySec < 0.2f) delaySec = 0.2f;

  const unsigned long now = millis();
  const unsigned long base = immediateBase ? now : (cloudActive ? cloudEndMs : now);
  cloudNextStartMs = base + static_cast<unsigned long>(delaySec * 1000.0f);
}

void stopActiveCloud() {
  cloudActive = false;
  cloudEndMs = 0;
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    cloudCurrentDimPercent[ch] = 0;
  }
}

void resetCloudSimulationRuntime(bool keepNextSchedule = false) {
  stopActiveCloud();
  if (!keepNextSchedule) {
    scheduleNextCloud(true);
  }
}

int cloudNextInSeconds() {
  if (!cloudSimEnabled || !cloudHasSelectedChannels()) return -1;
  if (cloudActive) return 0;
  const unsigned long now = millis();
  if (cloudNextStartMs <= now) return 0;
  return static_cast<int>((cloudNextStartMs - now) / 1000UL);
}

void updateCloudSimulation() {
  if (previewActive) {
    if (cloudActive) stopActiveCloud();
    return;
  }

  if (!cloudSimEnabled || !cloudHasSelectedChannels()) {
    if (cloudActive) stopActiveCloud();
    cloudNextStartMs = 0;
    return;
  }

  const unsigned long now = millis();

  if (cloudActive) {
    if (now >= cloudEndMs) {
      stopActiveCloud();
      scheduleNextCloud(false);
    }
    return;
  }

  if (cloudNextStartMs == 0) {
    scheduleNextCloud(true);
    return;
  }

  if (now < cloudNextStartMs) return;

  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    cloudCurrentDimPercent[ch] = 0;
    if ((cloudChannelsMask & (1u << ch)) == 0) continue;
    const float jitter = 0.8f + random01() * 0.4f;  // ongeveer de ingestelde waarde
    cloudCurrentDimPercent[ch] = clampCloudPercent(static_cast<int>(roundf(cloudDimPercent[ch] * jitter)));
  }

  float durationSec = sampleExponential(static_cast<float>(cloudAvgDurationSec));
  if (durationSec < 0.3f) durationSec = 0.3f;

  cloudActive = true;
  cloudEndMs = now + static_cast<unsigned long>(durationSec * 1000.0f);
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
      preset.channels[ch].points[i] = {times[i], static_cast<uint16_t>(dayShape[ch][i] * 16)};
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
  fillDefaultPreset(gData.presets[0], "Default preset");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    gData.channelColors[ch] = String(DEFAULT_COLORS[ch]);
    gData.channelMaxWatts[ch] = 0.0f;
  }
}

bool saveSchedulerData() {
  if (!fsReady) return false;

  DynamicJsonDocument doc(28672);
  doc["format"] = 2;  // 0-4095 schaal
  doc["activePreset"] = gData.activePreset;
  doc["simulationDaySeconds"] = simulationDaySeconds;
  doc["moonlightEnabled"]   = moonlightEnabled;
  doc["moonlightChannel"]   = moonlightChannel;
  doc["moonlightIntensity"] = moonlightIntensity;
  doc["masterBrightness"]   = masterBrightness;
  doc["cloudSimEnabled"]    = cloudSimEnabled;
  doc["cloudChannelsMask"]  = cloudChannelsMask;
  doc["cloudAvgDurationSec"] = cloudAvgDurationSec;
  doc["cloudEventsPerDay"]   = cloudEventsPerDay;

  JsonArray jCloudPct = doc.createNestedArray("cloudDimPercent");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    jCloudPct.add(cloudDimPercent[ch]);
  }

  JsonArray jColors = doc.createNestedArray("channelColors");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
    jColors.add(gData.channelColors[ch]);

  JsonArray jWatts = doc.createNestedArray("channelMaxWatts");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
    jWatts.add(gData.channelMaxWatts[ch]);

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
  moonlightEnabled   = doc["moonlightEnabled"]   | false;
  moonlightChannel   = doc["moonlightChannel"]   | (int8_t)-1;
  // Automatische migratie: waarden ≤255 zijn opgeslagen in oud 0-255 formaat
  {
    const bool oldFormat = !(doc["format"] | 0);
    const int rawIntensity = doc["moonlightIntensity"] | 492;
    moonlightIntensity = clampValue(oldFormat && rawIntensity <= 255 ? rawIntensity * 16 : rawIntensity);
  }
  {
    float b = doc["masterBrightness"] | 1.0f;
    masterBrightness = (b < 0.0f) ? 0.0f : (b > 2.0f) ? 2.0f : b;
  }
  cloudSimEnabled    = doc["cloudSimEnabled"] | false;
  cloudChannelsMask  = static_cast<uint8_t>(doc["cloudChannelsMask"] | ALL_CHANNELS_MASK) & ALL_CHANNELS_MASK;
  cloudAvgDurationSec = clampCloudDurationSec(doc["cloudAvgDurationSec"] | 5);
  cloudEventsPerDay   = clampCloudEventsPerDay(doc["cloudEventsPerDay"] | 100);
  JsonArray jCloudPct = doc["cloudDimPercent"].as<JsonArray>();
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    cloudDimPercent[ch] = (!jCloudPct.isNull() && ch < jCloudPct.size())
                              ? clampCloudPercent(jCloudPct[ch] | 50)
                              : 50;
    cloudCurrentDimPercent[ch] = 0;
  }

  JsonArray jColors = doc["channelColors"].as<JsonArray>();
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    if (!jColors.isNull() && ch < jColors.size())
      gData.channelColors[ch] = String((const char *)(jColors[ch] | DEFAULT_COLORS[ch]));
    else
      gData.channelColors[ch] = String(DEFAULT_COLORS[ch]);
  }

  JsonArray jWatts = doc["channelMaxWatts"].as<JsonArray>();
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    gData.channelMaxWatts[ch] = (!jWatts.isNull() && ch < jWatts.size()) ? (float)(jWatts[ch] | 0.0f) : 0.0f;
    if (gData.channelMaxWatts[ch] < 0.0f) gData.channelMaxWatts[ch] = 0.0f;
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
          // Automatische migratie: waarden ≤255 zijn opgeslagen in oud 0-255 formaat
          const bool oldFormat = !(doc["format"] | 0);
          const int rawVal = point["value"] | 0;
          curve.points[curve.pointCount].value = clampValue(oldFormat && rawVal <= 255 ? rawVal * 16 : rawVal);
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

// ─── MQTT Config ──────────────────────────────────────────────────────────────

bool saveMqttConfig() {
  if (!fsReady) return false;
  DynamicJsonDocument doc(1024);
  doc["enabled"]  = gMqttConfig.enabled;
  doc["broker"]   = gMqttConfig.broker;
  doc["port"]     = gMqttConfig.port;
  doc["username"] = gMqttConfig.username;
  doc["password"] = gMqttConfig.password;
  File f = LittleFS.open(MQTT_FILE, FILE_WRITE);
  if (!f) return false;
  bool ok = serializeJsonPretty(doc, f) > 0;
  f.close();
  return ok;
}

void loadMqttConfig() {
  gMqttConfig = MqttConfigData{};
  if (!fsReady || !LittleFS.exists(MQTT_FILE)) return;
  File f = LittleFS.open(MQTT_FILE, FILE_READ);
  if (!f) return;
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  gMqttConfig.enabled  = doc["enabled"]  | false;
  gMqttConfig.broker   = String((const char *)(doc["broker"]   | ""));
  gMqttConfig.port     = doc["port"]     | 1883;
  gMqttConfig.username = String((const char *)(doc["username"] | ""));
  gMqttConfig.password = String((const char *)(doc["password"] | ""));
}

String mqttDeviceId() {
  const uint64_t mac = ESP.getEfuseMac();
  char buf[7];
  snprintf(buf, sizeof(buf), "%06lx", static_cast<unsigned long>(mac & 0xFFFFFF));
  return String(buf);
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

  Serial.println("[WIFI] Verbinden mislukt.");
  return false;
}

void setupTimeSync() {
  if (ntpConfigured) return;
  configTzTime(gWifiConfig.timezone.c_str(), "pool.ntp.org", "time.nist.gov", "europe.pool.ntp.org");
  ntpConfigured = true;
  Serial.println("[NTP] Tijd sync gestart.");
}

void setupOta() {
  if (otaConfigured) return;

  ArduinoOTA.setHostname(DEVICE_HOSTNAME);
  ArduinoOTA.setPassword(gWifiConfig.otaPassword.c_str());
  ArduinoOTA.setTimeout(30000);
  ArduinoOTA.setMdnsEnabled(true);
  ArduinoOTA.onStart([]() {
    otaInProgress = true;
    debugWasEnabledBeforeOta = debugEnabled;
    debugEnabled = false;
    server.stop();
    if (apModeActive) dnsServer.stop();
    Serial.println("[OTA] Start");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned int lastPercent = 999;
    if (progress == 0) lastPercent = 0;
    const unsigned int percent = total > 0 ? (progress * 100U) / total : 0U;
    if (percent >= lastPercent + 10U || percent == 100U) {
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
    debugEnabled = debugWasEnabledBeforeOta;
    Serial.printf("[OTA] Fout %u\n", err);
    server.begin();
    if (apModeActive) dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  });
  ArduinoOTA.begin();
  otaConfigured = true;
  Serial.println("[OTA] Actief.");
}

// Forward declaration (setupMqtt defined after setSimulation block)
void setupMqtt();

void activateNetworkServicesIfConnected() {
  if (!wifiConnected()) return;
  setupTimeSync();
  setupOta();
  setupMqtt();
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

// ─── MQTT Runtime ─────────────────────────────────────────────────────────────

void mqttPublishState() {
  if (!mqttClient.connected()) return;
  const String id = mqttDeviceId();
  DynamicJsonDocument doc(512);
  doc["masterEnabled"]        = masterEnabled;
  doc["simulationActive"]     = simulationActive;
  doc["simulationDaySeconds"] = simulationDaySeconds;
  doc["activePreset"]         = gData.activePreset;
  doc["presetName"]           = gData.presetCount > 0
                                    ? gData.presets[gData.activePreset].name
                                    : String("");
  doc["nowMinute"]            = getMinuteOfDay();
  doc["masterBrightness"]       = masterBrightness;
  JsonArray outs = doc.createNestedArray("outputs");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) outs.add(currentOutputs[ch]);
  String payload;
  serializeJson(doc, payload);
  mqttClient.publish(("aqualed/" + id + "/state").c_str(), payload.c_str(), true);

  // Dedicated lamp state (JSON schema vereist puur JSON zonder templates)
  DynamicJsonDocument lamp(128);
  lamp["state"] = masterEnabled ? "ON" : "OFF";
  lamp["brightness"] = (uint8_t)round(masterBrightness / 2.0f * 255.0f);
  String lampPayload;
  serializeJson(lamp, lampPayload);
  mqttClient.publish(("aqualed/" + id + "/lamp").c_str(), lampPayload.c_str(), true);
}

void mqttPublishDiscovery() {
  if (!mqttClient.connected()) return;
  const String id   = mqttDeviceId();
  const String base = "aqualed/" + id;
  const String avty = base + "/available";

  auto addDev = [&](DynamicJsonDocument &d) {
    JsonObject dev = d.createNestedObject("dev");
    JsonArray  ids = dev.createNestedArray("ids");
    ids.add(id);
    dev["name"] = String(DEVICE_HOSTNAME);
    dev["mdl"]  = "AquaLed Controller";
    dev["mf"]   = "DIY";
  };

  auto pub = [&](const String &type, const String &slug, DynamicJsonDocument &d) {
    const String topic = "homeassistant/" + type + "/" + id + "_" + slug + "/config";
    String buf;
    serializeJson(d, buf);
    if (!mqttClient.publish(topic.c_str(), buf.c_str(), true))
      Serial.printf("[MQTT] Discovery mislukt: %s (%u bytes)\n", slug.c_str(), buf.length());
    yield();
  };

  // Verwijder eventueel overgebleven oude entiteiten
  mqttClient.publish(("homeassistant/switch/" + id + "_master/config").c_str(), "", true);
  mqttClient.publish(("homeassistant/number/" + id + "_brightness/config").c_str(), "", true);
  yield();

  { // Dimbare lamp (JSON schema)
    DynamicJsonDocument d(512);
    d["name"]    = "AquaLed";
    d["uniq_id"] = id + "_lamp";
    d["schema"]      = "json";
    d["stat_t"]      = base + "/lamp";
    d["cmd_t"]       = base + "/lamp/set";
    d["brightness"]  = true;
    d["avty_t"]      = avty;
    addDev(d);
    pub("light", "lamp", d);
  }
  { // Simulatie schakelaar
    DynamicJsonDocument d(512);
    d["name"]    = "AquaLed Simulatie";
    d["uniq_id"] = id + "_simulation";
    d["stat_t"]  = base + "/state";
    d["val_tpl"] = "{{ 'ON' if value_json.simulationActive else 'OFF' }}";
    d["cmd_t"]   = base + "/simulation/set";
    d["avty_t"]  = avty;
    addDev(d);
    pub("switch", "simulation", d);
  }
  { // Preset selectie
    DynamicJsonDocument d(1024);
    d["name"]    = "AquaLed Preset";
    d["uniq_id"] = id + "_preset";
    d["stat_t"]  = base + "/state";
    d["val_tpl"] = "{{ value_json.presetName }}";
    d["cmd_t"]   = base + "/preset/set";
    d["avty_t"]  = avty;
    JsonArray opts = d.createNestedArray("options");
    for (uint8_t i = 0; i < gData.presetCount; ++i) opts.add(gData.presets[i].name);
    addDev(d);
    pub("select", "preset", d);
  }
  { // Helderheid — verborgen, aangestuurd via lamp
    // (enkel bewaard voor automaties die /brightness/set gebruiken)
  }
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) { // Kanaal sensoren
    DynamicJsonDocument d(512);
    d["name"]    = String("AquaLed Kanaal ") + (ch + 1);
    d["uniq_id"] = id + "_ch" + (ch + 1);
    d["stat_t"]  = base + "/state";
    d["val_tpl"] = String("{{ (value_json.outputs[") + ch + String("] / 4095 * 100) | round(0) | int }}");
    d["avty_t"]  = avty;
    d["unit_of_measurement"] = "%";
    d["icon"]    = "mdi:brightness-percent";
    addDev(d);
    pub("sensor", String("ch") + (ch + 1), d);
    yield();
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String t(topic);
  String msg;
  msg.reserve(length);
  for (unsigned int i = 0; i < length; ++i) msg += static_cast<char>(payload[i]);
  const String base = "aqualed/" + mqttDeviceId();
  if (t == base + "/master/set") {
    masterEnabled = (msg == "ON");
    Serial.printf("[MQTT] master→%s\n", masterEnabled ? "AAN" : "UIT");
    mqttPublishState();
  } else if (t == base + "/simulation/set") {
    setSimulation(msg == "ON", simulationDaySeconds);
    mqttPublishState();
  } else if (t == base + "/preset/set") {
    for (uint8_t i = 0; i < gData.presetCount; ++i) {
      if (gData.presets[i].name == msg) {
        gData.activePreset = i;
        saveSchedulerData();
        break;
      }
    }
    mqttPublishState();
  } else if (t == base + "/brightness/set") {
    float pct = msg.toFloat();
    masterBrightness = constrain(pct / 100.0f, 0.0f, 2.0f);
    saveSchedulerData();
    Serial.printf("[MQTT] helderheid→%.0f%%\n", pct);
    mqttPublishState();
  } else if (t == base + "/lamp/set") {
    DynamicJsonDocument cmd(256);
    if (!deserializeJson(cmd, msg)) {
      if (cmd.containsKey("state")) {
        masterEnabled = (String((const char *)cmd["state"]) == "ON");
      }
      if (cmd.containsKey("brightness")) {
        float bri = cmd["brightness"].as<float>();
        // HA stuurt 0-255; 255 = 200% (max)
        masterBrightness = constrain(bri / 255.0f * 2.0f, 0.0f, 2.0f);
        saveSchedulerData();
      }
      Serial.printf("[MQTT] lamp→%s %.0f%%\n", masterEnabled ? "AAN" : "UIT", masterBrightness * 100);
      mqttPublishState();
    }
  }
}

bool reconnectMqtt() {
  const String id   = mqttDeviceId();
  const String base = "aqualed/" + id;
  const String avty = base + "/available";
  bool ok;
  if (gMqttConfig.username.isEmpty()) {
    ok = mqttClient.connect(id.c_str(), avty.c_str(), 0, true, "offline");
  } else {
    ok = mqttClient.connect(id.c_str(),
                            gMqttConfig.username.c_str(),
                            gMqttConfig.password.c_str(),
                            avty.c_str(), 0, true, "offline");
  }
  if (ok) {
    mqttClient.publish(avty.c_str(), "online", true);
    mqttClient.subscribe((base + "/master/set").c_str());
    mqttClient.subscribe((base + "/simulation/set").c_str());
    mqttClient.subscribe((base + "/preset/set").c_str());
    mqttClient.subscribe((base + "/brightness/set").c_str());
    mqttClient.subscribe((base + "/lamp/set").c_str());
    mqttPublishDiscovery();
    mqttPublishState();
    Serial.println("[MQTT] Verbonden.");
  } else {
    Serial.printf("[MQTT] Verbinding mislukt, rc=%d\n", mqttClient.state());
  }
  return ok;
}

void mqttConnectIfNeeded() {
  if (!gMqttConfig.enabled || gMqttConfig.broker.isEmpty()) return;
  if (!wifiConnected() || mqttClient.connected()) return;
  const unsigned long now = millis();
  if (now - lastMqttReconnectMs < 15000) return;
  lastMqttReconnectMs = now;
  reconnectMqtt();
}

void setupMqtt() {
  if (!gMqttConfig.enabled || gMqttConfig.broker.isEmpty()) return;
  mqttClient.setBufferSize(1024);
  mqttClient.setServer(gMqttConfig.broker.c_str(), gMqttConfig.port);
  mqttClient.setCallback(mqttCallback);
  lastMqttReconnectMs = 0;
  Serial.printf("[MQTT] Ingesteld: %s:%u\n", gMqttConfig.broker.c_str(), gMqttConfig.port);
}

uint16_t evaluateCurve(const ChannelCurve &curve, float minuteOfDay) {
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

// Gamma 2.8 correctie: perceptueel lineair dimmen over volledig 12-bit bereik
// Input: 0-4095 (perceptueel), output: 0-4095 (lineaire PWM duty)
uint16_t gammaCorrectedDuty(float value) {
  if (value <= 0.0f) return 0;
  if (value >= 4095.0f) return PWM_MAX_DUTY;
  float normalized = value / 4095.0f;
  float corrected = powf(normalized, 2.8f);
  return static_cast<uint16_t>(roundf(corrected * PWM_MAX_DUTY));
}

// Schrijf een reeds berekende 12-bit PWM waarde (0-4095) direct naar het kanaal
void writePwm(uint8_t channel, uint16_t value) {
  ledcWrite(channel, value > PWM_MAX_DUTY ? PWM_MAX_DUTY : value);
}

void writePwmFloat(uint8_t channel, float value) {
  if (value <= 0.0f) { ledcWrite(channel, 0); return; }
  if (value >= static_cast<float>(PWM_MAX_DUTY)) { ledcWrite(channel, PWM_MAX_DUTY); return; }
  ledcWrite(channel, static_cast<uint16_t>(roundf(value)));
}

void updateOutputs() {
  // Fade in perceptuele ruimte (0-4095): 2 sec van max naar 0 = 4095/40 ticks ≈ 102.4 per tick
  // Een waarde van bijv. 900/4095 fadet in 900/102.4 ≈ 9 ticks = 440ms — altijd zichtbaar vloeiend
  constexpr float MAX_STEP = 102.4f;

  if (gData.presetCount == 0) return;

  // Bij directe preview-waarden: outputs vasthouden, niet opnieuw berekenen
  if (previewActive && previewDirect) return;

  const Preset &active = gData.presets[gData.activePreset];
  float minute = getMinuteOfDay();
  moonlightCurrentlyActive = false;

  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    // Target in perceptuele 0-4095 ruimte (zelfde schaal als curve-punten)
    float target = masterEnabled
        ? static_cast<float>(evaluateCurve(active.channels[ch], minute))
        : 0.0f;
    // Maanlicht: maanwaarde is minimumhelderheid in perceptuele ruimte
    if (moonlightEnabled && moonlightChannel >= 0 && ch == static_cast<uint8_t>(moonlightChannel)) {
      if (masterEnabled) {
        const float moonTarget = moonlightIntensity * calcMoonPhase();
        if (moonTarget > target) {
          target = moonTarget;
          moonlightCurrentlyActive = true;
        }
      }
    }
    // Helderheidsscaling: schaal target met masterBrightness, limiteer op 4095
    target = fminf(target * masterBrightness, 4095.0f);
    // Wolken simulatie: dim geselecteerde kanalen met (ongeveer) ingestelde percentages.
    if (cloudActive && (cloudChannelsMask & (1u << ch))) {
      target *= (100.0f - cloudCurrentDimPercent[ch]) / 100.0f;
    }
    // Altijd faden in perceptuele ruimte — behalve bij directe preview (slider slepen)
    if (previewActive && previewDirect) {
      smoothOutputs[ch] = target;
    } else {
      float diff = target - smoothOutputs[ch];
      if (fabsf(diff) <= MAX_STEP) {
        smoothOutputs[ch] = target;
      } else {
        smoothOutputs[ch] += (diff > 0.0f ? MAX_STEP : -MAX_STEP);
      }
    }
    // Gamma-correctie alleen bij het schrijven naar PWM
    writePwm(ch, gammaCorrectedDuty(smoothOutputs[ch]));
    currentOutputs[ch] = static_cast<uint16_t>(roundf(smoothOutputs[ch]));
  }
}

void printStatusToSerial() {
  float minute = getMinuteOfDay();
  int hh = static_cast<int>(minute) / 60;
  int mm = static_cast<int>(minute) % 60;

  Serial.printf("[STAT] %02d:%02d | Preset %u: %s | Out:", hh, mm, gData.activePreset,
                gData.presets[gData.activePreset].name.c_str());
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    uint16_t duty = ledcRead(ch);
    Serial.printf(" ch%u=%u(%u/4095)", ch + 1, currentOutputs[ch], duty);
  }
  if (moonlightEnabled && moonlightChannel >= 0) {
    float phase = calcMoonPhase();
    float brightness = moonlightIntensity * phase / 4095.0f * 100.0f;
    Serial.printf(" | Maan: ch%d brandt op %.1f%% (fase %.0f%%)",
                  moonlightChannel + 1, brightness, phase * 100.0f);
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
  doc["timezone"]    = gWifiConfig.timezone;
  doc["masterEnabled"]  = masterEnabled;
  doc["mqttEnabled"]    = gMqttConfig.enabled;
  doc["mqttConnected"]  = mqttClient.connected();
  doc["mqttBroker"]     = gMqttConfig.broker;
  doc["mqttPort"]       = gMqttConfig.port;
  doc["mqttUsername"]   = gMqttConfig.username;
  doc["mqttDeviceId"]   = mqttDeviceId();
  doc["version"]        = FIRMWARE_VERSION;
  doc["uptimeSec"]      = millis() / 1000UL;
  doc["moonlightEnabled"]        = moonlightEnabled;
  doc["moonlightChannel"]        = moonlightChannel;
  doc["moonlightIntensity"]      = moonlightIntensity;
  doc["moonPhase"]               = calcMoonPhase();
  doc["moonlightActive"]         = moonlightCurrentlyActive;
  doc["masterBrightness"]        = masterBrightness;
  doc["cloudSimEnabled"]         = cloudSimEnabled;
  doc["cloudActive"]             = cloudActive;
  doc["cloudChannelsMask"]       = cloudChannelsMask;
  doc["cloudAvgDurationSec"]     = cloudAvgDurationSec;
  doc["cloudEventsPerDay"]       = cloudEventsPerDay;
  doc["cloudNextInSec"]          = cloudNextInSeconds();

  JsonArray jCloudPct = doc.createNestedArray("cloudDimPercent");
  JsonArray jCloudCurrentPct = doc.createNestedArray("cloudCurrentDimPercent");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    jCloudPct.add(cloudDimPercent[ch]);
    jCloudCurrentPct.add(cloudCurrentDimPercent[ch]);
  }

  JsonArray jColors = doc.createNestedArray("channelColors");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
    jColors.add(gData.channelColors[ch]);

  JsonArray jWatts2 = doc.createNestedArray("channelMaxWatts");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
    jWatts2.add(gData.channelMaxWatts[ch]);

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
  DynamicJsonDocument doc(768);
  doc["nowMinute"] = getMinuteOfDay();
  doc["dateTime"] = currentDateTimeText();
  doc["simulationActive"] = simulationActive;
  doc["simulationDaySeconds"] = simulationDaySeconds;
  doc["previewActive"] = previewActive;
  doc["masterEnabled"] = masterEnabled;
  doc["masterBrightness"] = masterBrightness;
  doc["cloudSimEnabled"] = cloudSimEnabled;
  doc["cloudActive"] = cloudActive;
  doc["cloudNextInSec"] = cloudNextInSeconds();
  doc["cloudEventsPerDay"] = cloudEventsPerDay;
  doc["cloudAvgDurationSec"] = cloudAvgDurationSec;
  JsonArray outL = doc.createNestedArray("outputs");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) outL.add(currentOutputs[ch]);
  JsonArray jWattsL = doc.createNestedArray("channelMaxWatts");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) jWattsL.add(gData.channelMaxWatts[ch]);
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
    resp["error"] = "kan laatste preset niet verwijderen";
    return sendJson(400, resp);
  }

  for (uint8_t i = index; i < gData.presetCount - 1; ++i) {
    gData.presets[i] = gData.presets[i + 1];
  }
  gData.presetCount--;

  if (gData.activePreset >= gData.presetCount) {
    gData.activePreset = gData.presetCount - 1;
  } else if (gData.activePreset > index) {
    gData.activePreset--;
  }

  saveSchedulerData();

  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  resp["presetCount"] = gData.presetCount;
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
  String password = String((const char *)(body["password"] | ""));
  String otaPassword = body["otaPassword"].isNull()
                           ? gWifiConfig.otaPassword
                           : String((const char *)(body["otaPassword"] | ""));
  String timezone = body["timezone"].isNull()
                        ? gWifiConfig.timezone
                        : String((const char *)(body["timezone"] | TZ_INFO));
  ssid.trim();
  otaPassword.trim();
  timezone.trim();

  if (ssid.isEmpty()) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "ssid required";
    return sendJson(400, resp);
  }

  gWifiConfig.ssid = ssid;
  gWifiConfig.password = password;
  if (!otaPassword.isEmpty()) {
    gWifiConfig.otaPassword = otaPassword;
  }
  if (!timezone.isEmpty()) {
    gWifiConfig.timezone = timezone;
    setenv("TZ", timezone.c_str(), 1);
    tzset();
  }
  saveWifiConfig(gWifiConfig);

  bool connected = connectWifiStation(60);
  if (connected) {
    otaConfigured = false;
    activateNetworkServicesIfConnected();
    stopConfigAp();
  } else {
    startConfigAp();
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

  JsonArray wArr = body["channelMaxWatts"].as<JsonArray>();
  if (!wArr.isNull() && wArr.size() == LED_CHANNEL_COUNT) {
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
      float w = wArr[ch] | 0.0f;
      gData.channelMaxWatts[ch] = w < 0.0f ? 0.0f : w;
    }
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

  if (enabled) {
    previewActive = false;
  }

  setSimulation(enabled, daySeconds);

  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  resp["simulationActive"] = simulationActive;
  resp["simulationDaySeconds"] = simulationDaySeconds;
  resp["nowMinute"] = getMinuteOfDay();
  sendJson(200, resp);
}

void handlePreviewSet() {
  DynamicJsonDocument body(1024);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid json";
    return sendJson(400, resp);
  }

  bool enabled = body["enabled"] | false;
  if (enabled) {
    if (simulationActive) {
      setSimulation(false, simulationDaySeconds);
    }
    previewMinute = clampMinute(body["minute"] | 0);
    previewActive = true;

    // Als directe output-waarden meegegeven zijn, gebruik die i.p.v. evaluateCurve
    JsonArray directOutputs = body["outputs"].as<JsonArray>();
    if (!directOutputs.isNull() && directOutputs.size() == LED_CHANNEL_COUNT) {
      previewDirect = true;
      for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        // Directe outputs van de JS zijn in 0-4095 perceptuele schaal
        const uint16_t val = clampValue(directOutputs[ch] | 0);
        smoothOutputs[ch] = static_cast<float>(val);
        currentOutputs[ch] = val;
        writePwm(ch, gammaCorrectedDuty(smoothOutputs[ch]));
      }
    } else {
      previewDirect = false;
      updateOutputs();
    }
  } else {
    previewActive = false;
    previewDirect = false;
    Serial.println("[PREVIEW] Uit");
    updateOutputs();
  }

  DynamicJsonDocument resp(512);
  resp["ok"] = true;
  resp["previewActive"] = previewActive;
  resp["nowMinute"] = getMinuteOfDay();
  JsonArray out = resp.createNestedArray("outputs");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) out.add(currentOutputs[ch]);
  sendJson(200, resp);
}

void handleMasterSet() {
  DynamicJsonDocument body(256);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false; resp["error"] = "invalid json";
    return sendJson(400, resp);
  }
  masterEnabled = body["enabled"] | masterEnabled;
  Serial.printf("[MASTER] %s\n", masterEnabled ? "AAN" : "UIT");
  mqttPublishState();
  DynamicJsonDocument resp(256);
  resp["ok"]            = true;
  resp["masterEnabled"] = masterEnabled;
  sendJson(200, resp);
}

void handleScheduleExport() {
  DynamicJsonDocument doc(28672);
  doc["activePreset"]         = gData.activePreset;
  doc["simulationDaySeconds"] = simulationDaySeconds;
  doc["cloudSimEnabled"]      = cloudSimEnabled;
  doc["cloudChannelsMask"]    = cloudChannelsMask;
  doc["cloudAvgDurationSec"]  = cloudAvgDurationSec;
  doc["cloudEventsPerDay"]    = cloudEventsPerDay;
  JsonArray jCloudPct = doc.createNestedArray("cloudDimPercent");
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) jCloudPct.add(cloudDimPercent[ch]);
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
        JsonObject pt = points.createNestedObject();
        pt["minute"] = curve.points[i].minute;
        pt["value"]  = curve.points[i].value;
      }
    }
  }
  String payload;
  serializeJsonPretty(doc, payload);
  server.sendHeader("Content-Disposition", "attachment; filename=\"aqualed-presets.json\"");
  server.send(200, "application/json", payload);
}

void handleScheduleImport() {
  DynamicJsonDocument body(28672);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false; resp["error"] = "invalid json";
    return sendJson(400, resp);
  }
  JsonArrayConst presets = body["presets"].as<JsonArrayConst>();
  if (presets.isNull() || presets.size() == 0) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false; resp["error"] = "geen presets gevonden";
    return sendJson(400, resp);
  }
  const uint8_t count = min<uint8_t>(presets.size(), MAX_PRESETS);
  SchedulerData newData{};
  newData.presetCount  = count;
  newData.activePreset = min<uint8_t>(body["activePreset"] | 0, count - 1);
  // channel kleuren overnemen uit bestand of bestaande kleuren behouden
  JsonArrayConst jColors = body["channelColors"].as<JsonArrayConst>();
  for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
    if (!jColors.isNull() && ch < jColors.size())
      newData.channelColors[ch] = String((const char *)(jColors[ch] | ""));
    else
      newData.channelColors[ch] = gData.channelColors[ch];
  }
  bool ok = true;
  for (uint8_t p = 0; p < count; ++p) {
    if (!parsePresetFromJson(presets[p], newData.presets[p])) { ok = false; break; }
  }
  if (!ok) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false; resp["error"] = "ongeldig preset formaat in bestand";
    return sendJson(400, resp);
  }
  gData = newData;
  if (!body["simulationDaySeconds"].isNull())
    simulationDaySeconds = clampSimulationSeconds(body["simulationDaySeconds"]);
  if (!body["cloudSimEnabled"].isNull())
    cloudSimEnabled = body["cloudSimEnabled"] | cloudSimEnabled;
  if (!body["cloudChannelsMask"].isNull())
    cloudChannelsMask = static_cast<uint8_t>(body["cloudChannelsMask"].as<int>()) & ALL_CHANNELS_MASK;
  if (!body["cloudAvgDurationSec"].isNull())
    cloudAvgDurationSec = clampCloudDurationSec(body["cloudAvgDurationSec"]);
  if (!body["cloudEventsPerDay"].isNull())
    cloudEventsPerDay = clampCloudEventsPerDay(body["cloudEventsPerDay"]);
  JsonArrayConst jCloudPct = body["cloudDimPercent"].as<JsonArrayConst>();
  if (!jCloudPct.isNull() && jCloudPct.size() == LED_CHANNEL_COUNT) {
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
      cloudDimPercent[ch] = clampCloudPercent(jCloudPct[ch] | cloudDimPercent[ch]);
    }
  }
  resetCloudSimulationRuntime(false);
  saveSchedulerData();
  DynamicJsonDocument resp(256);
  resp["ok"]          = true;
  resp["presetCount"] = gData.presetCount;
  sendJson(200, resp);
}

void handleMqttSave() {
  DynamicJsonDocument body(1024);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false; resp["error"] = "invalid json";
    return sendJson(400, resp);
  }
  if (!body["enabled"].isNull())  gMqttConfig.enabled  = body["enabled"]  | false;
  if (!body["broker"].isNull())   gMqttConfig.broker   = String((const char *)(body["broker"]   | ""));
  if (!body["port"].isNull())     gMqttConfig.port     = body["port"]     | 1883;
  if (!body["username"].isNull()) gMqttConfig.username = String((const char *)(body["username"] | ""));
  if (!body["password"].isNull()) gMqttConfig.password = String((const char *)(body["password"] | ""));
  gMqttConfig.broker.trim();
  gMqttConfig.username.trim();
  saveMqttConfig();
  if (mqttClient.connected()) mqttClient.disconnect();
  setupMqtt();
  DynamicJsonDocument resp(256);
  resp["ok"]           = true;
  resp["mqttEnabled"]  = gMqttConfig.enabled;
  resp["mqttConnected"] = false;
  sendJson(200, resp);
}

void handleMoonlightSave() {
  DynamicJsonDocument body(256);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false; resp["error"] = "invalid json";
    return sendJson(400, resp);
  }
  if (!body["enabled"].isNull())   moonlightEnabled   = body["enabled"]   | false;
  if (!body["channel"].isNull())   moonlightChannel   = static_cast<int8_t>(body["channel"].as<int>());
  if (!body["intensity"].isNull()) moonlightIntensity = clampValue(body["intensity"] | 30);
  saveSchedulerData();
  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  sendJson(200, resp);
}

void handleCloudSave() {
  DynamicJsonDocument body(1024);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false;
    resp["error"] = "invalid json";
    return sendJson(400, resp);
  }

  if (!body["enabled"].isNull()) {
    cloudSimEnabled = body["enabled"] | false;
  }

  if (!body["avgDurationSec"].isNull()) {
    cloudAvgDurationSec = clampCloudDurationSec(body["avgDurationSec"] | cloudAvgDurationSec);
  }

  if (!body["eventsPerDay"].isNull()) {
    cloudEventsPerDay = clampCloudEventsPerDay(body["eventsPerDay"] | cloudEventsPerDay);
  }

  if (!body["channelsMask"].isNull()) {
    cloudChannelsMask = static_cast<uint8_t>(body["channelsMask"].as<int>()) & ALL_CHANNELS_MASK;
  }

  JsonArray dimPct = body["dimPercent"].as<JsonArray>();
  if (!dimPct.isNull() && dimPct.size() == LED_CHANNEL_COUNT) {
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
      cloudDimPercent[ch] = clampCloudPercent(dimPct[ch] | cloudDimPercent[ch]);
    }
  }

  resetCloudSimulationRuntime(false);
  saveSchedulerData();

  DynamicJsonDocument resp(512);
  resp["ok"] = true;
  resp["cloudSimEnabled"] = cloudSimEnabled;
  resp["cloudChannelsMask"] = cloudChannelsMask;
  resp["cloudAvgDurationSec"] = cloudAvgDurationSec;
  resp["cloudEventsPerDay"] = cloudEventsPerDay;
  resp["cloudNextInSec"] = cloudNextInSeconds();
  sendJson(200, resp);
}

void handleBrightnessSet() {
  DynamicJsonDocument body(256);
  if (deserializeJson(body, server.arg("plain"))) {
    DynamicJsonDocument resp(256);
    resp["ok"] = false; resp["error"] = "invalid json";
    return sendJson(400, resp);
  }
  float b = body["brightness"] | masterBrightness;
  masterBrightness = (b < 0.0f) ? 0.0f : (b > 2.0f) ? 2.0f : b;
  saveSchedulerData();
  Serial.printf("[BRIGHTNESS] %.2f\n", masterBrightness);
  DynamicJsonDocument resp(256);
  resp["ok"] = true;
  resp["masterBrightness"] = masterBrightness;
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
  server.on("/api/preview/set", HTTP_POST, handlePreviewSet);
  server.on("/api/preset/upsert", HTTP_POST, handlePresetUpsert);
  server.on("/api/preset/select", HTTP_POST, handlePresetSelect);
  server.on("/api/preset/delete", HTTP_POST, handlePresetDelete);
  server.on("/api/wifi/save", HTTP_POST, handleWifiSave);
  server.on("/api/time/set", HTTP_POST, handleTimeSet);
  server.on("/api/simulation/set", HTTP_POST, handleSimulationSet);
  server.on("/api/colors/save",    HTTP_POST, handleColorsSave);
  server.on("/api/master/set",        HTTP_POST, handleMasterSet);
  server.on("/api/brightness/set",    HTTP_POST, handleBrightnessSet);
  server.on("/api/mqtt/save",         HTTP_POST, handleMqttSave);
  server.on("/api/moonlight/save",    HTTP_POST, handleMoonlightSave);
  server.on("/api/cloud/save",        HTTP_POST, handleCloudSave);
  server.on("/api/schedule/export",   HTTP_GET,  handleScheduleExport);
  server.on("/api/schedule/import",   HTTP_POST, handleScheduleImport);

  server.on("/api/ota/upload", HTTP_POST, []() {
    DynamicJsonDocument resp(256);
    resp["ok"] = !Update.hasError();
    if (Update.hasError()) {
      resp["error"] = "update failed";
      sendJson(500, resp);
    } else {
      resp["message"] = "Update succesvol, herstart...";
      sendJson(200, resp);
      delay(500);
      ESP.restart();
    }
  }, []() {
    HTTPUpload &upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("[OTA-WEB] Start: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("[OTA-WEB] Succes: %u bytes\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });

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
  const unsigned long now = millis();
  if (wifiConnected()) return;
  if (now - lastWifiRetryMs < WIFI_RETRY_MS) return;
  lastWifiRetryMs = now;

  if (!hasWifiCredentials(gWifiConfig)) {
    startConfigAp();
    return;
  }

  Serial.println("[WIFI] Reconnect poging...");
  bool connected = connectWifiStation(20);
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

void handleButton() {
  bool pressed = digitalRead(BUTTON_PIN) == LOW;
  unsigned long now = millis();

  if (pressed && !buttonPressed) {
    buttonPressed = true;
    buttonPressedMs = now;
  } else if (!pressed && buttonPressed) {
    buttonPressed = false;
    unsigned long held = now - buttonPressedMs;
    if (held >= BUTTON_DEBOUNCE_MS && held < BUTTON_LONG_PRESS_MS) {
      // Kort indrukken: master aan/uit toggle
      masterEnabled = !masterEnabled;
      Serial.printf("[BTN] Master %s\n", masterEnabled ? "AAN" : "UIT");
      mqttPublishState();
    } else if (held >= BUTTON_LONG_PRESS_MS) {
      // Lang indrukken: WiFi setup AP aan/uit toggle
      if (apModeActive) {
        stopConfigAp();
        Serial.println("[BTN] Setup AP gestopt.");
      } else {
        startConfigAp();
        Serial.println("[BTN] Setup AP gestart.");
      }
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

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.println("[BOOT] Button op GPIO0 actief.");

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
  loadMqttConfig();

  WiFi.mode(WIFI_STA);

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
  resetCloudSimulationRuntime(false);
}

void loop() {
  if (wifiConnected() && otaConfigured) ArduinoOTA.handle();

  if (otaInProgress) {
    yield();
    return;
  }

  server.handleClient();

  if (apModeActive) dnsServer.processNextRequest();

  if (wifiConnected() && gMqttConfig.enabled) {
    if (mqttClient.connected()) {
      mqttClient.loop();
      const unsigned long mqttNow = millis();
      if (mqttNow - lastMqttPublishMs >= 600000) {
        lastMqttPublishMs = mqttNow;
        mqttPublishState();
      }
    } else {
      mqttConnectIfNeeded();
    }
  }

  ensureWifiLink();
  handleSerialCli();
  handleButton();

  const unsigned long now = millis();
  updateCloudSimulation();
  if (now - lastPwmUpdateMs >= PWM_UPDATE_MS) {
    lastPwmUpdateMs = now;
    updateOutputs();
  }

  if (debugEnabled && now - lastDebugMs >= DEBUG_PRINT_MS) {
    lastDebugMs = now;
    printStatusToSerial();
  }
}
