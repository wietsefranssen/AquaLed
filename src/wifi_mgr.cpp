#include <ArduinoOTA.h>
#include "wifi_mgr.h"
#include "mqtt.h"     // setupMqtt()
#include "storage.h"  // saveSchedulerData()

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
    manualTimeSetMs      = millis();
    manualTimeActive     = true;
    Serial.printf("[TIME] Handmatige tijd gezet op %02u:%02u\n", hour, minute);
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

bool connectWifiStation(uint16_t retryCycles) {
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
        otaInProgress             = true;
        debugWasEnabledBeforeOta  = debugEnabled;
        debugEnabled              = false;
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
        debugEnabled  = debugWasEnabledBeforeOta;
        Serial.println("[OTA] Klaar, herstart...");
    });
    ArduinoOTA.onError([](ota_error_t err) {
        otaInProgress = false;
        debugEnabled  = debugWasEnabledBeforeOta;
        Serial.printf("[OTA] Fout %u\n", err);
        server.begin();
        if (apModeActive) dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    });
    ArduinoOTA.begin();
    otaConfigured = true;
    Serial.println("[OTA] Actief.");
}

void activateNetworkServicesIfConnected() {
    if (!wifiConnected()) return;
    setupTimeSync();
    setupOta();
    setupMqtt();
}

void otaLoop() {
    if (wifiConnected() && otaConfigured) ArduinoOTA.handle();
}

// ─── Tijd / simulatie ─────────────────────────────────────────────────────────

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
    const float dayMs   = static_cast<float>(simulationDaySeconds) * 1000.0f;
    float minute        = simulationStartMinute + (elapsedMs / dayMs) * 1440.0f;
    while (minute >= 1440.0f) minute -= 1440.0f;
    while (minute < 0.0f)     minute += 1440.0f;
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
            simulationStartMs     = millis();
        } else {
            simulationStartMinute = static_cast<uint16_t>(roundf(getSimulatedMinuteOfDay()));
            simulationStartMs     = millis();
        }
        simulationActive = true;
        Serial.printf("[SIM] Aan: 1 dag in %u sec, start=%u min\n",
                      simulationDaySeconds, simulationStartMinute);
    } else {
        const uint16_t frozenMinute = static_cast<uint16_t>(roundf(getSimulatedMinuteOfDay()));
        simulationActive         = false;
        manualTimeBaseMinute     = frozenMinute;
        manualTimeSetMs          = millis();
        manualTimeActive         = true;
        Serial.println("[SIM] Uit: huidige simulatietijd vastgezet als handmatige tijd.");
    }

    saveSchedulerData();
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
