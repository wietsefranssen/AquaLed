#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"

// ─── Constants ────────────────────────────────────────────────────────────────

constexpr uint16_t PWM_FREQ       = 5000;
constexpr uint8_t  PWM_RESOLUTION = 12;
constexpr uint16_t PWM_MAX_DUTY   = (1 << PWM_RESOLUTION) - 1;

constexpr uint8_t MAX_POINTS  = 16;
constexpr uint8_t MAX_PRESETS = 10;

constexpr char SCHEDULE_FILE[] = "/schedule.json";
constexpr char WIFI_FILE[]     = "/wifi.json";
constexpr char MQTT_FILE[]     = "/mqtt.json";

constexpr unsigned long PWM_UPDATE_MS     = 50;
constexpr unsigned long DEBUG_PRINT_MS    = 5000;
constexpr unsigned long WIFI_RETRY_MS     = 20000;
constexpr byte          DNS_PORT          = 53;
constexpr uint8_t       ALL_CHANNELS_MASK = (1u << LED_CHANNEL_COUNT) - 1u;

constexpr double MOON_CYCLE_DAYS = 29.53059;

constexpr const char* DEFAULT_COLORS[LED_CHANNEL_COUNT] = {
    "#1f7a8c", "#2d936c", "#8f6c4e", "#ba5a31", "#7b4fa3"};

// ─── Types ────────────────────────────────────────────────────────────────────

struct KeyPoint {
    uint16_t minute;
    uint16_t value;
};

struct ChannelCurve {
    uint8_t  pointCount;
    KeyPoint points[MAX_POINTS];
};

struct Preset {
    String       name;
    ChannelCurve channels[LED_CHANNEL_COUNT];
};

struct SchedulerData {
    uint8_t presetCount;
    uint8_t activePreset;
    Preset  presets[MAX_PRESETS];
    String  channelColors[LED_CHANNEL_COUNT];
    float   channelMaxWatts[LED_CHANNEL_COUNT];
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

struct CloudRuntime {
    bool          active         = false;
    unsigned long startMs        = 0;
    unsigned long endMs          = 0;
    unsigned long nextStartMs    = 0;
    uint8_t       peakDimPercent = 0;
};

// ─── Inline helpers ──────────────────────────────────────────────────────────

inline uint16_t clampMinute(int minute) {
    if (minute < 0)    return 0;
    if (minute > 1439) return 1439;
    return static_cast<uint16_t>(minute);
}

inline uint16_t clampValue(int value) {
    if (value < 0)    return 0;
    if (value > 4095) return 4095;
    return static_cast<uint16_t>(value);
}

inline float smoothStep(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

inline uint16_t clampSimulationSeconds(int seconds) {
    if (seconds < 5)    return 5;
    if (seconds > 3600) return 3600;
    return static_cast<uint16_t>(seconds);
}

inline bool isPlaceholderSsid(const String &ssid) {
    return ssid.isEmpty() || ssid == "YOUR_WIFI_SSID";
}

inline bool hasWifiCredentials(const WifiConfigData &cfg) {
    return !isPlaceholderSsid(cfg.ssid);
}

// ─── Global variable declarations ────────────────────────────────────────────

extern SchedulerData  gData;
extern WifiConfigData gWifiConfig;
extern MqttConfigData gMqttConfig;

extern WebServer server;
extern DNSServer dnsServer;
extern String    cliBuffer;

extern bool     debugEnabled;
extern bool     fsReady;
extern bool     apModeActive;
extern bool     ntpConfigured;
extern bool     otaConfigured;
extern bool     manualTimeActive;
extern bool     simulationActive;
extern bool     otaInProgress;
extern bool     debugWasEnabledBeforeOta;
extern bool     previewActive;
extern bool     previewDirect;
extern uint16_t previewMinute;

extern uint16_t      manualTimeBaseMinute;
extern unsigned long manualTimeSetMs;
extern uint16_t      simulationStartMinute;
extern unsigned long simulationStartMs;
extern uint16_t      simulationDaySeconds;

extern uint16_t currentOutputs[LED_CHANNEL_COUNT];
extern float    smoothOutputs[LED_CHANNEL_COUNT];
extern bool     masterEnabled;

extern bool     moonlightEnabled;
extern int8_t   moonlightChannel;
extern uint16_t moonlightIntensity;
extern bool     moonlightCurrentlyActive;
extern float    masterBrightness;

extern bool     cloudSimEnabled;
extern bool     cloudChannelEnabled[LED_CHANNEL_COUNT];
extern uint16_t cloudAvgDurationSec[LED_CHANNEL_COUNT];
extern uint16_t cloudMinDurationSec[LED_CHANNEL_COUNT];
extern uint16_t cloudEventsPerDay[LED_CHANNEL_COUNT];
extern uint8_t  cloudDimPercent[LED_CHANNEL_COUNT];
extern uint8_t  cloudCurrentDimPercent[LED_CHANNEL_COUNT];
extern CloudRuntime cloudRuntime[LED_CHANNEL_COUNT];

extern WiFiClient   mqttWifiClient;
extern PubSubClient mqttClient;

extern unsigned long lastMqttPublishMs;
extern unsigned long lastMqttReconnectMs;
extern unsigned long lastPwmUpdateMs;
extern unsigned long lastDebugMs;
extern unsigned long lastWifiRetryMs;

extern bool          buttonPressed;
extern unsigned long buttonPressedMs;
