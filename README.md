# IHC-Output-Bridge


IHC Output Bridge via ESPHomeDenne bridge gør det muligt at styre op til 8 originale LK IHC Output-moduler (i alt 64 udgange) ved hjælp af en ESP32. Kommunikationen foregår via MQTT, hvilket sikrer integration med Homey Pro, Home Assistant og andre platforme.

## 🚀 Funktioner
- Emulering af IHC-protokol: Præcis timing via ESP32 hardware-timere.
- 64 Udgange: Understøtter fuld adressering af 8 moduler med 8 udgange hver.
- MQTT Integration: Auto-discovery via Home Assistant/Homey standarden.
- Web Interface: Indbygget webserver til manuel test af alle udgange.

## 🛠 Hardware Krav
- ESP32 WROOM (eller lignende ESP32 dev kit)
- IHC Output moduler.
- Logic Level Shifter (Anbefales til konvertering fra 3.3V til 5V TTL).
- Fælles GND: Meget vigtigt for stabilt signal.

## Installation
1. Installer MQTT Broker, MQTT Client og MQTT Hub på din Homey.
2. Opret user og password i brokeren
3. Opret forbindelse mellem broker og client.
4. Tast IP-adressen for brokeren ind i .yaml filen
5. Tast user og password ind i .yaml filen

## 📂 Filstruktur
Placer filerne i din ESPHome-mappe som følger:  
/esphome/  
- ihc_controller.yaml (1-8 eller 9-16)  
- secrets.yaml  
- custom_component/ 
     - ihc_output/  
       - __ __init__ __.py  
       - ihc_output.h  
       - ihc_output.cpp  

## Kompilering og Flash
1. Tilslut din ESP32 via USB.
2. Kør i terminal: esphome run ihc_controller.yaml
3. Ved første flash: Hold BOOT-knappen nede når terminalen skriver "Connecting...". Slip efter ca. 3 sekunder

## Pin-konfiguration
- Modul 1  GPIO 13
- Modul 2  GPIO 14
- Modul 3  GPIO 16
- Modul 4  GPIO 17
- Modul 5  GPIO 25
- Modul 6  GPIO 26
- Modul 7  GPIO 27
- Modul 8  GPIO 32

## 🔗 Integration med Homey Pro (via MQTT)
ESPHome-appen til Homey afventer opdatering (Q1 2026), indtil da kan MQTT benyttes.

2. ESP32 sender automatisk "Discovery"-beskeder under prefixet homeassistant.
3. Tilføj device manuelt via MQTT Hub.

## 📡 MQTT Topics
Du kan styre enhederne manuelt via topics (X=modul, Y=udgang):
- Command: ihc_bridge/switch/ihcoutput_X_Y/command (Payload: ON/OFF).
- State: ihc_bridge/switch/ihcoutput_X_Y/state (Returværdi: ON/OFF).
