#include <LittleFS.h>
#include <ArduinoJson.h>
#include "storage.h"
#include "led.h"    // initDefaultData, sortAndNormalizeCurve
#include "cloud.h"  // clampCloudDurationSec etc.

// ─── Scheduler data ──────────────────────────────────────────────────────────

bool saveSchedulerData() {
    if (!fsReady) return false;

    DynamicJsonDocument doc(28672);
    doc["format"]                = 2;  // 0-4095 schaal
    doc["activePreset"]          = gData.activePreset;
    doc["simulationDaySeconds"]  = simulationDaySeconds;
    doc["moonlightEnabled"]      = moonlightEnabled;
    doc["moonlightChannel"]      = moonlightChannel;
    doc["moonlightIntensity"]    = moonlightIntensity;
    doc["masterBrightness"]      = masterBrightness;
    doc["cloudSimEnabled"]       = cloudSimEnabled;

    JsonArray jCloudEnabled = doc.createNestedArray("cloudChannelEnabled");
    JsonArray jCloudAvgSec  = doc.createNestedArray("cloudAvgDurationSec");
    JsonArray jCloudMinSec  = doc.createNestedArray("cloudMinDurationSec");
    JsonArray jCloudPerDay  = doc.createNestedArray("cloudEventsPerDay");
    JsonArray jCloudPct     = doc.createNestedArray("cloudDimPercent");
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        jCloudEnabled.add(cloudChannelEnabled[ch]);
        jCloudAvgSec.add(cloudAvgDurationSec[ch]);
        jCloudMinSec.add(cloudMinDurationSec[ch]);
        jCloudPerDay.add(cloudEventsPerDay[ch]);
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
        JsonObject jp       = presets.createNestedObject();
        jp["name"]          = gData.presets[p].name;
        JsonArray channels  = jp.createNestedArray("channels");

        for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
            JsonArray points          = channels.createNestedArray();
            const ChannelCurve &curve = gData.presets[p].channels[ch];
            for (uint8_t i = 0; i < curve.pointCount; ++i) {
                JsonObject point  = points.createNestedObject();
                point["minute"]   = curve.points[i].minute;
                point["value"]    = curve.points[i].value;
            }
        }
    }

    File f  = LittleFS.open(SCHEDULE_FILE, FILE_WRITE);
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

    gData.presetCount  = min<uint8_t>(presets.size(), MAX_PRESETS);
    gData.activePreset = min<uint8_t>(doc["activePreset"] | 0, gData.presetCount - 1);
    simulationDaySeconds = clampSimulationSeconds(doc["simulationDaySeconds"] | simulationDaySeconds);
    moonlightEnabled   = doc["moonlightEnabled"]   | false;
    moonlightChannel   = doc["moonlightChannel"]   | (int8_t)-1;

    // Automatische migratie: waarden ≤255 zijn opgeslagen in oud 0-255 formaat
    {
        const bool oldFormat    = !(doc["format"] | 0);
        const int  rawIntensity = doc["moonlightIntensity"] | 492;
        moonlightIntensity = clampValue(oldFormat && rawIntensity <= 255 ? rawIntensity * 16 : rawIntensity);
    }
    {
        float b = doc["masterBrightness"] | 1.0f;
        masterBrightness = (b < 0.0f) ? 0.0f : (b > 2.0f) ? 2.0f : b;
    }
    cloudSimEnabled = doc["cloudSimEnabled"] | false;

    JsonArray jCloudEnabled = doc["cloudChannelEnabled"].as<JsonArray>();
    JsonArray jCloudAvgSec  = doc["cloudAvgDurationSec"].as<JsonArray>();
    JsonArray jCloudMinSec  = doc["cloudMinDurationSec"].as<JsonArray>();
    JsonArray jCloudPerDay  = doc["cloudEventsPerDay"].as<JsonArray>();
    JsonArray jCloudPct     = doc["cloudDimPercent"].as<JsonArray>();

    // Migratie van oude cloud instellingen (globaal + channelsMask) naar per-kanaal
    const uint8_t  legacyMask   = static_cast<uint8_t>(doc["cloudChannelsMask"] | ALL_CHANNELS_MASK) & ALL_CHANNELS_MASK;
    const uint16_t legacyAvgSec = clampCloudDurationSec(doc["cloudAvgDurationSec"].is<JsonArray>() ? 30 : (doc["cloudAvgDurationSec"] | 30));
    const uint16_t legacyPerDay = clampCloudEventsPerDay(doc["cloudEventsPerDay"].is<JsonArray>() ? 100 : (doc["cloudEventsPerDay"] | 100));

    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        cloudChannelEnabled[ch] = (!jCloudEnabled.isNull() && ch < jCloudEnabled.size())
                                      ? static_cast<bool>(jCloudEnabled[ch] | true)
                                      : ((legacyMask & (1u << ch)) != 0);
        cloudAvgDurationSec[ch] = (!jCloudAvgSec.isNull() && ch < jCloudAvgSec.size())
                                      ? clampCloudDurationSec(jCloudAvgSec[ch] | 30)
                                      : legacyAvgSec;
        cloudMinDurationSec[ch] = (!jCloudMinSec.isNull() && ch < jCloudMinSec.size())
                                      ? clampCloudMinDurationSec(jCloudMinSec[ch] | 10)
                                      : 10;
        if (cloudAvgDurationSec[ch] < cloudMinDurationSec[ch])
            cloudAvgDurationSec[ch] = cloudMinDurationSec[ch];
        cloudEventsPerDay[ch] = (!jCloudPerDay.isNull() && ch < jCloudPerDay.size())
                                    ? clampCloudEventsPerDay(jCloudPerDay[ch] | 100)
                                    : legacyPerDay;
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
        gData.channelMaxWatts[ch] = (!jWatts.isNull() && ch < jWatts.size())
                                        ? (float)(jWatts[ch] | 0.0f)
                                        : 0.0f;
        if (gData.channelMaxWatts[ch] < 0.0f) gData.channelMaxWatts[ch] = 0.0f;
    }

    for (uint8_t p = 0; p < gData.presetCount; ++p) {
        JsonObject jp          = presets[p].as<JsonObject>();
        gData.presets[p].name  = String((const char *)(jp["name"] | "Preset"));

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
                    const int  rawVal    = point["value"] | 0;
                    curve.points[curve.pointCount].value = clampValue(oldFormat && rawVal <= 255 ? rawVal * 16 : rawVal);
                    curve.pointCount++;
                }
            }
            sortAndNormalizeCurve(curve);
        }
    }

    return true;
}

// ─── WiFi config ─────────────────────────────────────────────────────────────

bool saveWifiConfig(const WifiConfigData &cfg) {
    if (!fsReady) return false;

    DynamicJsonDocument doc(1024);
    doc["ssid"]        = cfg.ssid;
    doc["password"]    = cfg.password;
    doc["otaPassword"] = cfg.otaPassword;
    doc["timezone"]    = cfg.timezone;

    File f  = LittleFS.open(WIFI_FILE, FILE_WRITE);
    if (!f) return false;
    bool ok = serializeJsonPretty(doc, f) > 0;
    f.close();
    return ok;
}

void loadWifiConfig(WifiConfigData &cfg) {
    cfg.ssid        = "";
    cfg.password    = "";
    cfg.otaPassword = String(OTA_PASSWORD);
    cfg.timezone    = String(TZ_INFO);

    if (fsReady && LittleFS.exists(WIFI_FILE)) {
        File f = LittleFS.open(WIFI_FILE, FILE_READ);
        if (f) {
            DynamicJsonDocument doc(1024);
            DeserializationError err = deserializeJson(doc, f);
            f.close();
            if (!err) {
                cfg.ssid        = String((const char *)(doc["ssid"]        | ""));
                cfg.password    = String((const char *)(doc["password"]    | ""));
                cfg.otaPassword = String((const char *)(doc["otaPassword"] | OTA_PASSWORD));
                cfg.timezone    = String((const char *)(doc["timezone"]    | TZ_INFO));
            }
        }
    }

    if (!hasWifiCredentials(cfg)) {
        cfg.ssid     = String(WIFI_SSID);
        cfg.password = String(WIFI_PASSWORD);
        if (isPlaceholderSsid(cfg.ssid)) {
            cfg.ssid     = "";
            cfg.password = "";
        }
    }
}

// ─── MQTT config ─────────────────────────────────────────────────────────────

bool saveMqttConfig() {
    if (!fsReady) return false;
    DynamicJsonDocument doc(1024);
    doc["enabled"]  = gMqttConfig.enabled;
    doc["broker"]   = gMqttConfig.broker;
    doc["port"]     = gMqttConfig.port;
    doc["username"] = gMqttConfig.username;
    doc["password"] = gMqttConfig.password;
    File f  = LittleFS.open(MQTT_FILE, FILE_WRITE);
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
