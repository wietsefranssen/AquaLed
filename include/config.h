#pragma once

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

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

#if defined(BOARD_PICO)
  // Gledopto ESP32 Pico
  static constexpr int LED_PINS[LED_CHANNEL_COUNT] = {25, 26, 27, 32, 33};
  static constexpr int BUTTON_PIN = 0;
#elif defined(BOARD_ESP32DEV)
  // Generic ESP32-dev and Gledopto-ledboard
  static constexpr int LED_PINS[LED_CHANNEL_COUNT] = {19, 18, 17, 16, 4};
  static constexpr int BUTTON_PIN = 0;
#else
  #error "Geen board gedefinieerd. Voeg -DBOARD_PICO of -DBOARD_ESP32DEV toe aan build_flags."
#endif
