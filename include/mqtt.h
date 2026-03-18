#pragma once

#include "globals.h"

String mqttDeviceId();
void   mqttPublishState();
void   mqttPublishDiscovery();
void   mqttCallback(char *topic, byte *payload, unsigned int length);
bool   reconnectMqtt();
void   mqttConnectIfNeeded();
void   setupMqtt();
