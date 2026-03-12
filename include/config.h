#pragma once

// Vul deze waarden in voor je eigen netwerk.
#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

#ifndef DEVICE_HOSTNAME
#define DEVICE_HOSTNAME "aqualed-controller"
#endif

#ifndef OTA_PASSWORD
#define OTA_PASSWORD "change-me"
#endif

// CET/CEST voor NL/BE
#ifndef TZ_INFO
#define TZ_INFO "CET-1CEST,M3.5.0/2,M10.5.0/3"
#endif

// Hardware instellingen
static constexpr int LED_CHANNEL_COUNT = 5;
static constexpr int LED_PINS[LED_CHANNEL_COUNT] = {16, 17, 18, 19, 21};
