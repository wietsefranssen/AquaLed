#include <ArduinoJson.h>
#include "mqtt.h"
#include "cloud.h"     // resetCloudSimulationRuntime()
#include "wifi_mgr.h"  // getMinuteOfDay(), wifiConnected()
#include "storage.h"   // saveSchedulerData()

String mqttDeviceId() {
    const uint64_t mac = ESP.getEfuseMac();
    char buf[7];
    snprintf(buf, sizeof(buf), "%06lx", static_cast<unsigned long>(mac & 0xFFFFFF));
    return String(buf);
}

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
    doc["masterBrightness"]     = masterBrightness;
    doc["moonlightEnabled"]     = moonlightEnabled;
    doc["moonlightChannel"]     = moonlightChannel;
    doc["moonlightIntensity"]   = moonlightIntensity;
    doc["cloudSimEnabled"]      = cloudSimEnabled;
    doc["cloudActive"]          = cloudActiveCount() > 0;
    doc["cloudNextInSec"]       = cloudNextInSeconds();
    JsonArray outs = doc.createNestedArray("outputs");
    for (uint8_t ch = 0; ch < LED_CHANNEL_COUNT; ++ch) outs.add(currentOutputs[ch]);
    String payload;
    serializeJson(doc, payload);
    mqttClient.publish(("aqualed/" + id + "/state").c_str(), payload.c_str(), true);

    // Dedicated lamp state
    DynamicJsonDocument lamp(128);
    lamp["state"]      = masterEnabled ? "ON" : "OFF";
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
        d["name"]       = "AquaLed";
        d["uniq_id"]    = id + "_lamp";
        d["schema"]     = "json";
        d["stat_t"]     = base + "/lamp";
        d["cmd_t"]      = base + "/lamp/set";
        d["brightness"] = true;
        d["avty_t"]     = avty;
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
    { // Maan simulatie schakelaar
        DynamicJsonDocument d(512);
        d["name"]    = "AquaLed Maanlicht Simulatie";
        d["uniq_id"] = id + "_moonlight";
        d["stat_t"]  = base + "/state";
        d["val_tpl"] = "{{ 'ON' if value_json.moonlightEnabled else 'OFF' }}";
        d["cmd_t"]   = base + "/moonlight/set";
        d["avty_t"]  = avty;
        d["icon"]    = "mdi:moon-waxing-crescent";
        addDev(d);
        pub("switch", "moonlight", d);
    }
    { // Wolken simulatie schakelaar
        DynamicJsonDocument d(512);
        d["name"]    = "AquaLed Wolken Simulatie";
        d["uniq_id"] = id + "_cloud";
        d["stat_t"]  = base + "/state";
        d["val_tpl"] = "{{ 'ON' if value_json.cloudSimEnabled else 'OFF' }}";
        d["cmd_t"]   = base + "/cloud/set";
        d["avty_t"]  = avty;
        d["icon"]    = "mdi:weather-cloudy";
        addDev(d);
        pub("switch", "cloud", d);
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
    } else if (t == base + "/moonlight/set") {
        DynamicJsonDocument cmd(256);
        if (!deserializeJson(cmd, msg)) {
            if (cmd.containsKey("enabled")) {
                moonlightEnabled = cmd["enabled"] | moonlightEnabled;
            }
            if (cmd.containsKey("channel")) {
                moonlightChannel = static_cast<int8_t>(cmd["channel"].as<int>());
            }
            if (cmd.containsKey("intensity")) {
                moonlightIntensity = clampValue(cmd["intensity"] | moonlightIntensity);
            }
        } else {
            moonlightEnabled = (msg == "ON");
        }
        saveSchedulerData();
        Serial.printf("[MQTT] moonlight→%s\n", moonlightEnabled ? "AAN" : "UIT");
        mqttPublishState();
    } else if (t == base + "/cloud/set") {
        DynamicJsonDocument cmd(256);
        if (!deserializeJson(cmd, msg)) {
            if (cmd.containsKey("enabled")) {
                cloudSimEnabled = cmd["enabled"] | cloudSimEnabled;
            }
        } else {
            cloudSimEnabled = (msg == "ON");
        }
        if (cloudSimEnabled) {
            resetCloudSimulationRuntime(false);
        }
        saveSchedulerData();
        Serial.printf("[MQTT] cloud→%s\n", cloudSimEnabled ? "AAN" : "UIT");
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
            if (cmd.containsKey("state"))
                masterEnabled = (String((const char *)cmd["state"]) == "ON");
            if (cmd.containsKey("brightness")) {
                float bri    = cmd["brightness"].as<float>();
                masterBrightness = constrain(bri / 255.0f * 2.0f, 0.0f, 2.0f);
                saveSchedulerData();
            }
            Serial.printf("[MQTT] lamp→%s %.0f%%\n",
                          masterEnabled ? "AAN" : "UIT", masterBrightness * 100);
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
        mqttClient.subscribe((base + "/moonlight/set").c_str());
        mqttClient.subscribe((base + "/cloud/set").c_str());
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
