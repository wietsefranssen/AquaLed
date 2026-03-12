# AquaLed ESP32 Controller (PlatformIO)

Dit project is een 5-kanaals LED controller voor ESP32 met:

- OTA updates (ArduinoOTA)
- Webinterface voor tijdcurves per kanaal
- Seriele CLI debug output
- 5x PWM output (0..255)
- Dagelijkse simulatie op basis van tijdlijn (00:00 -> 23:59)
- Meerdere presets die opgeslagen blijven in LittleFS

## Hardware

- ESP32 dev board
- 5 LED-kanalen op GPIO pins: 16, 17, 18, 19, 21

Pas pins aan in include/config.h als nodig.

## Installatie

1. Open dit project in VS Code met PlatformIO.
2. Vul WiFi, hostname en OTA password in include/config.h.
3. Build en upload via USB.
4. Open serial monitor (115200).
5. Noteer IP-adres uit de boot logs en open dat in je browser.

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

## Data opslag

Presets worden opgeslagen in LittleFS bestand:

- /schedule.json

Bij eerste start maakt de firmware een standaard preset aan.

## Belangrijk

- Intensiteit wordt intern vloeiend berekend met smooth interpolation per segment.
- Zonder geldige NTP tijd gebruikt de controller een fallback klok op basis van uptime.
