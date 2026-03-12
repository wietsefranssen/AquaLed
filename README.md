# AquaLed ESP32-PICO-D4 Controller (PlatformIO)

Dit project is een 5-kanaals LED controller voor ESP32 met:

- OTA updates (ArduinoOTA)
- Webinterface voor tijdcurves per kanaal
- Aparte settings pagina op /settings
- Seriele CLI debug output
- 5x PWM output (0..255)
- Dagelijkse simulatie op basis van tijdlijn (00:00 -> 23:59)
- Meerdere presets die opgeslagen blijven in LittleFS
- AP setup mode voor wifi-configuratie als STA niet verbindt
- Handmatige tijdinstelling via webinterface/CLI als NTP niet beschikbaar is

## Hardware

- ESP32-PICO-D4 (PlatformIO board: pico32)
- 5 LED-kanalen op GPIO pins: 16, 17, 18, 19, 21

Let op: gebruik geen flash-pinnen (GPIO 6..11) voor LED outputs.

Pas pins aan in include/config.h als nodig.

## Installatie

1. Open dit project in VS Code met PlatformIO.
2. Vul bij voorkeur alleen hostname en OTA password in include/config.h.
3. Wifi credentials kun je later invoeren via de settingspagina.
4. Build en upload via USB.
5. Open serial monitor (115200).
6. Bij mislukte wifi start de ESP een setup AP; verbind en ga naar /settings.

## OTA upload (optioneel)

In platformio.ini staan voorbeeldregels voor OTA upload. Zet deze aan en vul:

- upload_protocol = espota
- upload_port = ip-van-esp32
- upload_flags = --auth=ota-password

Daarna kun je uploaden zonder USB.

## CLI commando's (serial)

- help
- status
- list
- select <index>
- settime <HH:MM>
- ap on
- ap off
- debug on
- debug off
- save
- wifi

## Webinterface gebruik

- Per kanaal zie je een tijdlijn van 00:00 tot 23:59.
- Klik op de grafiek om een punt toe te voegen.
- Sleep een punt om tijd/intensiteit te wijzigen.
- Rechtsklik op een punt om te verwijderen.
- Opslaan als nieuwe preset of overschrijven van geselecteerde preset.
- Activeer een preset direct op de ESP32.
- Ga naar /settings voor wifi-credentials en handmatige tijd.

## Data opslag

Presets worden opgeslagen in LittleFS bestand:

- /schedule.json
- /wifi.json

Bij eerste start maakt de firmware een standaard preset aan.

## Belangrijk

- Intensiteit wordt intern vloeiend berekend met smooth interpolation per segment.
- Zonder geldige NTP tijd gebruikt de controller een fallback klok op basis van uptime.
