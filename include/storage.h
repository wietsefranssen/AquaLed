#pragma once

#include "globals.h"

bool saveSchedulerData();
bool loadSchedulerData();
bool saveWifiConfig(const WifiConfigData &cfg);
void loadWifiConfig(WifiConfigData &cfg);
bool saveMqttConfig();
void loadMqttConfig();
