#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Update.h>
#include "web_handlers.h"
#include "index_html.h"
#include "settings_html.h"
#include "cloud.h"
#include "led.h"
#include "storage.h"
#include "wifi_mgr.h"
#include "mqtt.h"

// ─── Helpers ─────────────────────────────────────────────────────────────────

void sendJson(int code, const JsonDocument &doc) {
    String payload;
    serializeJson(doc, payload);
    server.send(code, "application/json", payload);
}

static bool parsePresetFromJson(JsonVariantConst root, Preset &outPreset) {
    outPreset.name = String((const char *)(root["name"] | "Preset"));

    JsonArrayConst channels = root["channels"].as<JsonArrayConst>();
    if (channels.isNull() || channels.size() != LED_CHANNEL_COUNT) return false;

    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        JsonArrayConst points = channels[ch].as<JsonArrayConst>();
        if (points.isNull() || points.size() == 0) return false;

        ChannelCurve &curve = outPreset.channels[ch];
        curve.pointCount    = 0;

        for (JsonObjectConst point : points) {
            if (curve.pointCount >= MAX_POINTS) break;
            curve.points[curve.pointCount].minute = clampMinute(point["minute"] | 0);
            curve.points[curve.pointCount].value  = clampValue(point["value"]   | 0);
            curve.pointCount++;
        }
        sortAndNormalizeCurve(curve);
    }
    return true;
}

// ─── State ───────────────────────────────────────────────────────────────────

String stateJson() {
    DynamicJsonDocument doc(32768);
    doc["activePreset"]          = gData.activePreset;
    doc["nowMinute"]             = getMinuteOfDay();
    doc["wifiConnected"]         = wifiConnected();
    doc["ssid"]                  = wifiConnected() ? WiFi.SSID() : gWifiConfig.ssid;
    doc["stationIp"]             = wifiConnected() ? WiFi.localIP().toString() : "0.0.0.0";
    doc["apMode"]                = apModeActive;
    doc["apIp"]                  = apModeActive ? WiFi.softAPIP().toString() : "0.0.0.0";
    doc["ntpSynced"]             = ntpTimeValid();
    doc["manualTime"]            = manualTimeActive;
    doc["simulationActive"]      = simulationActive;
    doc["simulationDaySeconds"]  = simulationDaySeconds;
    doc["previewActive"]         = previewActive;
    doc["dateTime"]              = currentDateTimeText();
    doc["otaPassword"]           = gWifiConfig.otaPassword;
    doc["timezone"]              = gWifiConfig.timezone;
    doc["masterEnabled"]         = masterEnabled;
    doc["mqttEnabled"]           = gMqttConfig.enabled;
    doc["mqttConnected"]         = mqttClient.connected();
    doc["mqttBroker"]            = gMqttConfig.broker;
    doc["mqttPort"]              = gMqttConfig.port;
    doc["mqttUsername"]          = gMqttConfig.username;
    doc["mqttDeviceId"]          = mqttDeviceId();
    doc["version"]               = FIRMWARE_VERSION;
    doc["uptimeSec"]             = millis() / 1000UL;
    doc["moonlightEnabled"]      = moonlightEnabled;
    doc["moonlightChannel"]      = moonlightChannel;
    doc["moonlightIntensity"]    = moonlightIntensity;
    doc["moonPhase"]             = calcMoonPhase();
    doc["moonlightActive"]       = moonlightCurrentlyActive;
    doc["masterBrightness"]      = masterBrightness;
    doc["cloudSimEnabled"]       = cloudSimEnabled;
    doc["cloudActive"]           = cloudActiveCount() > 0;
    doc["cloudActiveCount"]      = cloudActiveCount();
    doc["cloudNextInSec"]        = cloudNextInSeconds();

    uint32_t sumEvents = 0, sumAvgSec = 0;
    uint8_t  enabledCount = 0;
    JsonArray jCloudEnabled    = doc.createNestedArray("cloudChannelEnabled");
    JsonArray jCloudAvgSec     = doc.createNestedArray("cloudAvgDurationSec");
    JsonArray jCloudMinSec     = doc.createNestedArray("cloudMinDurationSec");
    JsonArray jCloudPerDay     = doc.createNestedArray("cloudEventsPerDay");
    JsonArray jCloudPct        = doc.createNestedArray("cloudDimPercent");
    JsonArray jCloudCurrentPct = doc.createNestedArray("cloudCurrentDimPercent");
    JsonArray jCloudNextSec    = doc.createNestedArray("cloudNextInSecPerChannel");
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        jCloudEnabled.add(cloudChannelEnabled[ch]);
        jCloudAvgSec.add(cloudAvgDurationSec[ch]);
        jCloudMinSec.add(cloudMinDurationSec[ch]);
        jCloudPerDay.add(cloudEventsPerDay[ch]);
        jCloudPct.add(cloudDimPercent[ch]);
        jCloudCurrentPct.add(cloudCurrentDimPercent[ch]);
        jCloudNextSec.add(cloudNextInSecondsForChannel(ch));
        if (cloudChannelEnabled[ch]) {
            enabledCount++;
            sumEvents += cloudEventsPerDay[ch];
            sumAvgSec += cloudAvgDurationSec[ch];
        }
    }
    doc["cloudEventsPerDayAvg"]    = enabledCount > 0 ? (sumEvents / enabledCount) : 0;
    doc["cloudAvgDurationSecAvg"]  = enabledCount > 0 ? (sumAvgSec / enabledCount) : 0;

    JsonArray jColors = doc.createNestedArray("channelColors");
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
        jColors.add(gData.channelColors[ch]);

    JsonArray jWatts = doc.createNestedArray("channelMaxWatts");
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
        jWatts.add(gData.channelMaxWatts[ch]);

    JsonArray out = doc.createNestedArray("outputs");
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) out.add(currentOutputs[ch]);

    JsonArray presets = doc.createNestedArray("presets");
    for (uint8_t p = 0; p < gData.presetCount; ++p) {
        JsonObject jp      = presets.createNestedObject();
        jp["name"]         = gData.presets[p].name;
        JsonArray channels = jp.createNestedArray("channels");

        for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
            JsonArray points          = channels.createNestedArray();
            const ChannelCurve &curve = gData.presets[p].channels[ch];
            for (uint8_t i = 0; i < curve.pointCount; ++i) {
                JsonObject point = points.createNestedObject();
                point["minute"]  = curve.points[i].minute;
                point["value"]   = curve.points[i].value;
            }
        }
    }

    String payload;
    serializeJson(doc, payload);
    return payload;
}

// ─── HTTP handlers ────────────────────────────────────────────────────────────

void handleGetState() {
    server.send(200, "application/json", stateJson());
}

void handleGetStateLight() {
    DynamicJsonDocument doc(768);
    doc["nowMinute"]            = getMinuteOfDay();
    doc["dateTime"]             = currentDateTimeText();
    doc["simulationActive"]     = simulationActive;
    doc["simulationDaySeconds"] = simulationDaySeconds;
    doc["previewActive"]        = previewActive;
    doc["masterEnabled"]        = masterEnabled;
    doc["masterBrightness"]     = masterBrightness;
    doc["cloudSimEnabled"]      = cloudSimEnabled;
    doc["cloudActive"]          = cloudActiveCount() > 0;
    doc["cloudActiveCount"]     = cloudActiveCount();
    doc["cloudNextInSec"]       = cloudNextInSeconds();
    uint32_t sumEvents = 0, sumAvgSec = 0;
    uint8_t  enabledCount = 0;
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        if (!cloudChannelEnabled[ch]) continue;
        enabledCount++;
        sumEvents += cloudEventsPerDay[ch];
        sumAvgSec += cloudAvgDurationSec[ch];
    }
    doc["cloudEventsPerDay"]    = enabledCount > 0 ? (sumEvents / enabledCount) : 0;
    doc["cloudAvgDurationSec"]  = enabledCount > 0 ? (sumAvgSec / enabledCount) : 0;
    JsonArray outL = doc.createNestedArray("outputs");
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) outL.add(currentOutputs[ch]);
    JsonArray jWattsL = doc.createNestedArray("channelMaxWatts");
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) jWattsL.add(gData.channelMaxWatts[ch]);
    sendJson(200, doc);
}

void handlePresetUpsert() {
    DynamicJsonDocument body(28672);
    if (deserializeJson(body, server.arg("plain"))) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "invalid json";
        return sendJson(400, resp);
    }

    Preset p;
    if (!parsePresetFromJson(body.as<JsonVariantConst>(), p)) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "invalid preset payload";
        return sendJson(400, resp);
    }

    int index   = body["index"] | -1;
    bool created = false;

    if (index < 0) {
        if (gData.presetCount >= MAX_PRESETS) {
            DynamicJsonDocument resp(256);
            resp["ok"] = false; resp["error"] = "max presets reached";
            return sendJson(400, resp);
        }
        gData.presets[gData.presetCount] = p;
        gData.activePreset               = gData.presetCount;
        gData.presetCount++;
        created = true;
    } else {
        if (index >= gData.presetCount) {
            DynamicJsonDocument resp(256);
            resp["ok"] = false; resp["error"] = "preset index out of range";
            return sendJson(400, resp);
        }
        gData.presets[index] = p;
        gData.activePreset   = index;
    }

    saveSchedulerData();

    DynamicJsonDocument resp(256);
    resp["ok"]           = true;
    resp["created"]      = created;
    resp["activePreset"] = gData.activePreset;
    sendJson(200, resp);
}

void handlePresetSelect() {
    DynamicJsonDocument body(512);
    if (deserializeJson(body, server.arg("plain"))) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "invalid json";
        return sendJson(400, resp);
    }

    int index = body["index"] | 0;
    if (index < 0 || index >= gData.presetCount) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "preset index out of range";
        return sendJson(400, resp);
    }

    gData.activePreset = index;
    saveSchedulerData();

    DynamicJsonDocument resp(256);
    resp["ok"]           = true;
    resp["activePreset"] = gData.activePreset;
    sendJson(200, resp);
}

void handlePresetDelete() {
    DynamicJsonDocument body(512);
    if (deserializeJson(body, server.arg("plain"))) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "invalid json";
        return sendJson(400, resp);
    }

    int index = body["index"] | -1;
    if (index < 0 || index >= gData.presetCount) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "preset index out of range";
        return sendJson(400, resp);
    }

    if (gData.presetCount <= 1) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "kan laatste preset niet verwijderen";
        return sendJson(400, resp);
    }

    for (uint8_t i = index; i < gData.presetCount - 1; ++i)
        gData.presets[i] = gData.presets[i + 1];
    gData.presetCount--;

    if (gData.activePreset >= gData.presetCount)
        gData.activePreset = gData.presetCount - 1;
    else if (gData.activePreset > index)
        gData.activePreset--;

    saveSchedulerData();

    DynamicJsonDocument resp(256);
    resp["ok"]           = true;
    resp["presetCount"]  = gData.presetCount;
    resp["activePreset"] = gData.activePreset;
    sendJson(200, resp);
}

void handleWifiSave() {
    DynamicJsonDocument body(1024);
    if (deserializeJson(body, server.arg("plain"))) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "invalid json";
        return sendJson(400, resp);
    }

    String ssid        = String((const char *)(body["ssid"] | ""));
    String password    = String((const char *)(body["password"] | ""));
    String otaPassword = body["otaPassword"].isNull()
                             ? gWifiConfig.otaPassword
                             : String((const char *)(body["otaPassword"] | ""));
    String timezone    = body["timezone"].isNull()
                             ? gWifiConfig.timezone
                             : String((const char *)(body["timezone"] | TZ_INFO));
    ssid.trim();
    otaPassword.trim();
    timezone.trim();

    if (ssid.isEmpty()) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "ssid required";
        return sendJson(400, resp);
    }

    gWifiConfig.ssid     = ssid;
    gWifiConfig.password = password;
    if (!otaPassword.isEmpty()) gWifiConfig.otaPassword = otaPassword;
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
    resp["ok"]        = true;
    resp["connected"] = connected;
    resp["apMode"]    = apModeActive;
    resp["stationIp"] = wifiConnected() ? WiFi.localIP().toString() : "0.0.0.0";
    resp["apIp"]      = apModeActive ? WiFi.softAPIP().toString() : "0.0.0.0";
    sendJson(200, resp);
}

void handleTimeSet() {
    DynamicJsonDocument body(512);
    if (deserializeJson(body, server.arg("plain"))) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "invalid json";
        return sendJson(400, resp);
    }

    int hour   = body["hour"]   | -1;
    int minute = body["minute"] | -1;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "invalid time";
        return sendJson(400, resp);
    }

    setManualTime(static_cast<uint8_t>(hour), static_cast<uint8_t>(minute));

    DynamicJsonDocument resp(256);
    resp["ok"]         = true;
    resp["manualTime"] = true;
    resp["nowMinute"]  = getMinuteOfDay();
    sendJson(200, resp);
}

void handleColorsSave() {
    DynamicJsonDocument body(1024);
    if (deserializeJson(body, server.arg("plain"))) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "invalid json";
        return sendJson(400, resp);
    }

    JsonArray arr = body["channelColors"].as<JsonArray>();
    if (arr.isNull() || arr.size() != LED_CHANNEL_COUNT) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "channelColors array required with 5 entries";
        return sendJson(400, resp);
    }

    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
        gData.channelColors[ch] = String((const char *)(arr[ch] | DEFAULT_COLORS[ch]));

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
        resp["ok"] = false; resp["error"] = "invalid json";
        return sendJson(400, resp);
    }

    bool enabled    = body["enabled"]    | false;
    int  daySeconds = body["daySeconds"] | simulationDaySeconds;

    if (enabled) previewActive = false;

    setSimulation(enabled, daySeconds);

    DynamicJsonDocument resp(256);
    resp["ok"]                  = true;
    resp["simulationActive"]    = simulationActive;
    resp["simulationDaySeconds"] = simulationDaySeconds;
    resp["nowMinute"]           = getMinuteOfDay();
    sendJson(200, resp);
}

void handlePreviewSet() {
    DynamicJsonDocument body(1024);
    if (deserializeJson(body, server.arg("plain"))) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "invalid json";
        return sendJson(400, resp);
    }

    bool enabled = body["enabled"] | false;
    if (enabled) {
        if (simulationActive) setSimulation(false, simulationDaySeconds);
        previewMinute = clampMinute(body["minute"] | 0);
        previewActive = true;

        JsonArray directOutputs = body["outputs"].as<JsonArray>();
        if (!directOutputs.isNull() && directOutputs.size() == LED_CHANNEL_COUNT) {
            previewDirect = true;
            for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
                const uint16_t val = clampValue(directOutputs[ch] | 0);
                smoothOutputs[ch]  = static_cast<float>(val);
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
    resp["ok"]            = true;
    resp["previewActive"] = previewActive;
    resp["nowMinute"]     = getMinuteOfDay();
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
    doc["activePreset"]          = gData.activePreset;
    doc["simulationDaySeconds"]  = simulationDaySeconds;
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
    JsonArray presets = doc.createNestedArray("presets");
    for (uint8_t p = 0; p < gData.presetCount; ++p) {
        JsonObject jp      = presets.createNestedObject();
        jp["name"]         = gData.presets[p].name;
        JsonArray channels = jp.createNestedArray("channels");
        for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
            JsonArray points          = channels.createNestedArray();
            const ChannelCurve &curve = gData.presets[p].channels[ch];
            for (uint8_t i = 0; i < curve.pointCount; ++i) {
                JsonObject pt = points.createNestedObject();
                pt["minute"]  = curve.points[i].minute;
                pt["value"]   = curve.points[i].value;
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

    JsonArrayConst jCloudEnabled = body["cloudChannelEnabled"].as<JsonArrayConst>();
    JsonArrayConst jCloudAvgSec  = body["cloudAvgDurationSec"].as<JsonArrayConst>();
    JsonArrayConst jCloudMinSec  = body["cloudMinDurationSec"].as<JsonArrayConst>();
    JsonArrayConst jCloudPerDay  = body["cloudEventsPerDay"].as<JsonArrayConst>();
    JsonArrayConst jCloudPct     = body["cloudDimPercent"].as<JsonArrayConst>();

    const uint8_t  legacyMask   = static_cast<uint8_t>(body["cloudChannelsMask"] | ALL_CHANNELS_MASK) & ALL_CHANNELS_MASK;
    const uint16_t legacyAvgSec = clampCloudDurationSec(body["cloudAvgDurationSec"].is<JsonArray>() ? 30 : (body["cloudAvgDurationSec"] | 30));
    const uint16_t legacyPerDay = clampCloudEventsPerDay(body["cloudEventsPerDay"].is<JsonArray>() ? 100 : (body["cloudEventsPerDay"] | 100));

    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
        if (!jCloudEnabled.isNull() && ch < jCloudEnabled.size())
            cloudChannelEnabled[ch] = jCloudEnabled[ch] | cloudChannelEnabled[ch];
        else if (!body["cloudChannelsMask"].isNull())
            cloudChannelEnabled[ch] = (legacyMask & (1u << ch)) != 0;

        if (!jCloudAvgSec.isNull() && ch < jCloudAvgSec.size())
            cloudAvgDurationSec[ch] = clampCloudDurationSec(jCloudAvgSec[ch] | cloudAvgDurationSec[ch]);
        else if (!body["cloudAvgDurationSec"].isNull() && !body["cloudAvgDurationSec"].is<JsonArray>())
            cloudAvgDurationSec[ch] = legacyAvgSec;

        if (!jCloudMinSec.isNull() && ch < jCloudMinSec.size())
            cloudMinDurationSec[ch] = clampCloudMinDurationSec(jCloudMinSec[ch] | cloudMinDurationSec[ch]);

        if (!jCloudPerDay.isNull() && ch < jCloudPerDay.size())
            cloudEventsPerDay[ch] = clampCloudEventsPerDay(jCloudPerDay[ch] | cloudEventsPerDay[ch]);
        else if (!body["cloudEventsPerDay"].isNull() && !body["cloudEventsPerDay"].is<JsonArray>())
            cloudEventsPerDay[ch] = legacyPerDay;

        if (!jCloudPct.isNull() && ch < jCloudPct.size())
            cloudDimPercent[ch] = clampCloudPercent(jCloudPct[ch] | cloudDimPercent[ch]);

        if (cloudAvgDurationSec[ch] < cloudMinDurationSec[ch])
            cloudAvgDurationSec[ch] = cloudMinDurationSec[ch];
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
    resp["ok"]            = true;
    resp["mqttEnabled"]   = gMqttConfig.enabled;
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
    DynamicJsonDocument body(2048);
    if (deserializeJson(body, server.arg("plain"))) {
        DynamicJsonDocument resp(256);
        resp["ok"] = false; resp["error"] = "invalid json";
        return sendJson(400, resp);
    }

    if (!body["enabled"].isNull()) cloudSimEnabled = body["enabled"] | false;

    JsonArray channels = body["channels"].as<JsonArray>();
    if (!channels.isNull() && channels.size() == LED_CHANNEL_COUNT) {
        Serial.println("=== CLOUD SAVE: Verwerking per-kanaal payload ===");
        for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
            JsonObject cfg = channels[ch].as<JsonObject>();
            Serial.printf("CH%d IN: enabled=%d, avg=%d, min=%d, events=%d, dim=%d\n",
                ch, cfg["enabled"].as<int>(), cfg["avgDurationSec"].as<int>(),
                cfg["minDurationSec"].as<int>(), cfg["eventsPerDay"].as<int>(),
                cfg["dimPercent"].as<int>());

            cloudChannelEnabled[ch] = cfg["enabled"].isNull()        ? cloudChannelEnabled[ch] : (cfg["enabled"] | cloudChannelEnabled[ch]);
            cloudAvgDurationSec[ch] = cfg["avgDurationSec"].isNull() ? cloudAvgDurationSec[ch] : clampCloudDurationSec(cfg["avgDurationSec"] | cloudAvgDurationSec[ch]);
            cloudMinDurationSec[ch] = cfg["minDurationSec"].isNull() ? cloudMinDurationSec[ch] : clampCloudMinDurationSec(cfg["minDurationSec"] | cloudMinDurationSec[ch]);
            cloudEventsPerDay[ch]   = cfg["eventsPerDay"].isNull()   ? cloudEventsPerDay[ch]   : clampCloudEventsPerDay(cfg["eventsPerDay"] | cloudEventsPerDay[ch]);
            cloudDimPercent[ch]     = cfg["dimPercent"].isNull()     ? cloudDimPercent[ch]     : clampCloudPercent(cfg["dimPercent"] | cloudDimPercent[ch]);
            if (cloudAvgDurationSec[ch] < cloudMinDurationSec[ch])
                cloudAvgDurationSec[ch] = cloudMinDurationSec[ch];

            Serial.printf("CH%d OUT: enabled=%d, avg=%d, min=%d, events=%d, dim=%d\n",
                ch, cloudChannelEnabled[ch], cloudAvgDurationSec[ch],
                cloudMinDurationSec[ch], cloudEventsPerDay[ch], cloudDimPercent[ch]);
        }
    } else {
        // Backward compatibility met oude payload
        if (!body["avgDurationSec"].isNull()) {
            const uint16_t avgSec = clampCloudDurationSec(body["avgDurationSec"] | 30);
            for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) cloudAvgDurationSec[ch] = avgSec;
        }
        if (!body["eventsPerDay"].isNull()) {
            const uint16_t perDay = clampCloudEventsPerDay(body["eventsPerDay"] | 100);
            for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) cloudEventsPerDay[ch] = perDay;
        }
        if (!body["channelsMask"].isNull()) {
            const uint8_t mask = static_cast<uint8_t>(body["channelsMask"].as<int>()) & ALL_CHANNELS_MASK;
            for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
                cloudChannelEnabled[ch] = (mask & (1u << ch)) != 0;
        }
        JsonArray dimPct = body["dimPercent"].as<JsonArray>();
        if (!dimPct.isNull() && dimPct.size() == LED_CHANNEL_COUNT) {
            for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch)
                cloudDimPercent[ch] = clampCloudPercent(dimPct[ch] | cloudDimPercent[ch]);
        }
        for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) {
            cloudMinDurationSec[ch] = clampCloudMinDurationSec(cloudMinDurationSec[ch]);
            if (cloudAvgDurationSec[ch] < cloudMinDurationSec[ch])
                cloudAvgDurationSec[ch] = cloudMinDurationSec[ch];
        }
    }

    resetCloudSimulationRuntime(false);
    saveSchedulerData();

    DynamicJsonDocument resp(512);
    resp["ok"]               = true;
    resp["cloudSimEnabled"]  = cloudSimEnabled;
    resp["cloudNextInSec"]   = cloudNextInSeconds();
    resp["cloudActiveCount"] = cloudActiveCount();
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
    resp["ok"]               = true;
    resp["masterBrightness"] = masterBrightness;
    sendJson(200, resp);
}

// ─── Server setup ─────────────────────────────────────────────────────────────

void setupWebServer() {
    if (WiFi.getMode() == WIFI_MODE_NULL) WiFi.mode(WIFI_STA);

    server.on("/",        HTTP_GET, []() { server.send_P(200, "text/html", INDEX_HTML); });
    server.on("/settings", HTTP_GET, []() { server.send_P(200, "text/html", SETTINGS_HTML); });

    server.on("/api/state",              HTTP_GET,  handleGetState);
    server.on("/api/state/light",        HTTP_GET,  handleGetStateLight);
    server.on("/api/preview/set",        HTTP_POST, handlePreviewSet);
    server.on("/api/preset/upsert",      HTTP_POST, handlePresetUpsert);
    server.on("/api/preset/select",      HTTP_POST, handlePresetSelect);
    server.on("/api/preset/delete",      HTTP_POST, handlePresetDelete);
    server.on("/api/wifi/save",          HTTP_POST, handleWifiSave);
    server.on("/api/time/set",           HTTP_POST, handleTimeSet);
    server.on("/api/simulation/set",     HTTP_POST, handleSimulationSet);
    server.on("/api/colors/save",        HTTP_POST, handleColorsSave);
    server.on("/api/master/set",         HTTP_POST, handleMasterSet);
    server.on("/api/brightness/set",     HTTP_POST, handleBrightnessSet);
    server.on("/api/mqtt/save",          HTTP_POST, handleMqttSave);
    server.on("/api/moonlight/save",     HTTP_POST, handleMoonlightSave);
    server.on("/api/cloud/save",         HTTP_POST, handleCloudSave);
    server.on("/api/schedule/export",    HTTP_GET,  handleScheduleExport);
    server.on("/api/schedule/import",    HTTP_POST, handleScheduleImport);

    server.on("/api/ota/upload", HTTP_POST,
        []() {
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
        },
        []() {
            HTTPUpload &upload = server.upload();
            if (upload.status == UPLOAD_FILE_START) {
                Serial.printf("[OTA-WEB] Start: %s\n", upload.filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
                    Update.printError(Serial);
            } else if (upload.status == UPLOAD_FILE_END) {
                if (Update.end(true))
                    Serial.printf("[OTA-WEB] Succes: %u bytes\n", upload.totalSize);
                else
                    Update.printError(Serial);
            }
        });

    server.onNotFound([]() {
        if (apModeActive) {
            server.sendHeader("Location", "/settings", true);
            server.send(302, "text/plain", "");
            return;
        }
        DynamicJsonDocument resp(256);
        resp["ok"]    = false;
        resp["error"] = "not found";
        sendJson(404, resp);
    });

    server.begin();
    Serial.println("[HTTP] Webserver gestart op poort 80.");
}
