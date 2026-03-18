#include "globals.h"

SchedulerData  gData{};
WifiConfigData gWifiConfig{};
MqttConfigData gMqttConfig{};

WebServer server(80);
DNSServer dnsServer;
String    cliBuffer;

bool     debugEnabled             = true;
bool     fsReady                  = false;
bool     apModeActive             = false;
bool     ntpConfigured            = false;
bool     otaConfigured            = false;
bool     manualTimeActive         = false;
bool     simulationActive         = false;
bool     otaInProgress            = false;
bool     debugWasEnabledBeforeOta = false;
bool     previewActive            = false;
bool     previewDirect            = false;
uint16_t previewMinute            = 0;

uint16_t      manualTimeBaseMinute  = 0;
unsigned long manualTimeSetMs       = 0;
uint16_t      simulationStartMinute = 0;
unsigned long simulationStartMs     = 0;
uint16_t      simulationDaySeconds  = 120;

uint16_t currentOutputs[LED_CHANNEL_COUNT] = {0, 0, 0, 0, 0};
float    smoothOutputs[LED_CHANNEL_COUNT]  = {0};
bool     masterEnabled                     = true;

bool     moonlightEnabled         = false;
int8_t   moonlightChannel         = -1;
uint16_t moonlightIntensity       = 492;
bool     moonlightCurrentlyActive = false;
float    masterBrightness         = 1.0f;

bool     cloudSimEnabled                              = false;
bool     cloudChannelEnabled[LED_CHANNEL_COUNT]       = {true, true, true, true, true};
uint16_t cloudAvgDurationSec[LED_CHANNEL_COUNT]       = {30, 30, 30, 30, 30};
uint16_t cloudMinDurationSec[LED_CHANNEL_COUNT]       = {10, 10, 10, 10, 10};
uint16_t cloudEventsPerDay[LED_CHANNEL_COUNT]         = {100, 100, 100, 100, 100};
uint8_t  cloudDimPercent[LED_CHANNEL_COUNT]           = {50, 50, 50, 50, 50};
uint8_t  cloudCurrentDimPercent[LED_CHANNEL_COUNT]    = {0, 0, 0, 0, 0};
CloudRuntime cloudRuntime[LED_CHANNEL_COUNT];

WiFiClient   mqttWifiClient;
PubSubClient mqttClient(mqttWifiClient);

unsigned long lastMqttPublishMs   = 0;
unsigned long lastMqttReconnectMs = 0;
unsigned long lastPwmUpdateMs     = 0;
unsigned long lastDebugMs         = 0;
unsigned long lastWifiRetryMs     = 0;

bool          buttonPressed   = false;
unsigned long buttonPressedMs = 0;
