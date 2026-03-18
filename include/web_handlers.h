#pragma once

#include "globals.h"
#include <ArduinoJson.h>

void sendJson(int code, const JsonDocument &doc);
String stateJson();
void setupWebServer();

void handleGetState();
void handleGetStateLight();
void handlePresetUpsert();
void handlePresetSelect();
void handlePresetDelete();
void handleWifiSave();
void handleTimeSet();
void handleColorsSave();
void handleSimulationSet();
void handlePreviewSet();
void handleMasterSet();
void handleScheduleExport();
void handleScheduleImport();
void handleMqttSave();
void handleMoonlightSave();
void handleCloudSave();
void handleBrightnessSet();
