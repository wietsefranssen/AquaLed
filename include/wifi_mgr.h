#pragma once

#include "globals.h"

bool   wifiConnected();
bool   ntpTimeValid();
String apSsid();
void   setManualTime(uint8_t hour, uint8_t minute);
void   startConfigAp();
void   stopConfigAp();
bool   connectWifiStation(uint16_t retryCycles = 80);
void   setupTimeSync();
void   setupOta();
void   activateNetworkServicesIfConnected();
void   otaLoop();  // roept ArduinoOTA.handle() aan indien van toepassing
float  getBaseMinuteOfDay();
float  getSimulatedMinuteOfDay();
float  getMinuteOfDay();
String currentDateTimeText();
void   setSimulation(bool enabled, int daySeconds);
void   ensureWifiLink();
