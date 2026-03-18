#include <Arduino.h>
#include <LittleFS.h>

#include "globals.h"
#include "cloud.h"
#include "led.h"
#include "storage.h"
#include "wifi_mgr.h"
#include "mqtt.h"
#include "web_handlers.h"

static constexpr unsigned long BUTTON_DEBOUNCE_MS   = 50;
static constexpr unsigned long BUTTON_LONG_PRESS_MS = 3000;

// ─── CLI ─────────────────────────────────────────────────────────────────────

static void printCliHelp() {
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

static void handleCliCommand(String cmd) {
  cmd.trim();
  if (cmd.isEmpty()) return;

  if (cmd == "help") {
    printCliHelp();
  } else if (cmd == "status") {
    printStatusToSerial();
  } else if (cmd == "list") {
    Serial.printf("[CLI] Presets (%u):\n", gData.presetCount);
    for (uint8_t i = 0; i < gData.presetCount; ++i)
      Serial.printf("  %u: %s%s\n", i, gData.presets[i].name.c_str(),
              (i == gData.activePreset ? " [active]" : ""));
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
    String t   = cmd.substring(8);
    int    sep = t.indexOf(':');
    if (sep > 0) {
      int hh = t.substring(0, sep).toInt();
      int mm = t.substring(sep + 1).toInt();
      if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59)
        setManualTime(static_cast<uint8_t>(hh), static_cast<uint8_t>(mm));
      else
        Serial.println("[CLI] Ongeldige tijd, gebruik HH:MM.");
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
    Serial.printf("[CLI] WiFi status: %d, STA IP: %s, AP: %s\n",
            WiFi.status(), WiFi.localIP().toString().c_str(),
            apModeActive ? WiFi.softAPIP().toString().c_str() : "off");
  } else {
    Serial.println("[CLI] Onbekend commando, gebruik help.");
  }
}

static void handleSerialCli() {
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

// ─── Button ──────────────────────────────────────────────────────────────────

static void handleButton() {
  bool          pressed = digitalRead(BUTTON_PIN) == LOW;
  unsigned long now     = millis();

  if (pressed && !buttonPressed) {
    buttonPressed   = true;
    buttonPressedMs = now;
  } else if (!pressed && buttonPressed) {
    buttonPressed = false;
    unsigned long held = now - buttonPressedMs;
    if (held >= BUTTON_DEBOUNCE_MS && held < BUTTON_LONG_PRESS_MS) {
      masterEnabled = !masterEnabled;
      Serial.printf("[BTN] Master %s\n", masterEnabled ? "AAN" : "UIT");
      mqttPublishState();
    } else if (held >= BUTTON_LONG_PRESS_MS) {
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

// ─── Arduino entrypoints ─────────────────────────────────────────────────────

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
  if (connected)
    activateNetworkServicesIfConnected();
  else
    startConfigAp();

  printCliHelp();
  Serial.println("[AquaLed] Klaar.");
  resetCloudSimulationRuntime(false);
}

void loop() {
  otaLoop();

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

