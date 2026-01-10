# IHC-Output-Bridge


IHC Output Bridge via ESPHomeDenne bridge gør det muligt at styre op til 8 originale LK IHC Output-moduler (i alt 64 udgange) ved hjælp af en ESP32. Kommunikationen foregår via MQTT, hvilket sikrer integration med Homey Pro, Home Assistant og andre platforme.

🚀 Funktioner
Emulering af IHC-protokol: Præcis timing via ESP32 hardware-timere.
64 Udgange: Understøtter fuld adressering af 8 moduler med 8 udgange hver.
MQTT Integration: Auto-discovery via Home Assistant/Homey standarden.
Web Interface: Indbygget webserver til manuel test af alle udgange.

🛠 Hardware Krav
ESP32 WROOM (eller lignende ESP32 dev kit).
IHC Output moduler.
Logic Level Shifter (Anbefales til konvertering fra 3.3V til 5V TTL).
Fælles GND: Meget vigtigt for stabilt signal.

Pin-konfiguration (ESP32 WROOM)
Modul 1  GPIO 13
Modul 2  GPIO 14
Modul 3  GPIO 16
Modul 4  GPIO 17
Modul 5  GPIO 25
Modul 6  GPIO 26
Modul 7  GPIO 27
Modul 8  GPIO 32
