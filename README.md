# IHC Output Bridge


IHC Output Bridge via ESPHome, Denne bridge gør det muligt at styre op til 8 IHC Output-moduler (i alt 64 udgange) ved hjælp af en ESP32. Kommunikationen foregår via MQTT, hvilket sikrer integration med Homey Pro, Home Assistant og andre platforme.

## Funktioner
- Emulering af IHC-protokol mellem IHC controller og IHC Output Modul.
- Understøtter fuld adressering af 8 moduler med 8 udgange hver i alt 64 udgange.
- MQTT Integration
- Web Interface: Indbygget webserver til manuel test af alle udgange.

## Hardware Krav
- ESP32 WROOM (eller lignende ESP32)
- IHC Output moduler.
- Logic Level Shifter (Til konvertering fra 3.3V til 5V TTL).
- Step down modul fra 24v til 3,3V (Til at forsyne ESP32)
- Step down modul fra 24v til 5V (Til at forsyne 5V siden på level shifter)
- Fælles GND: __Meget vigtigt__ for stabilt signal på alle GPIO udgange

## Installation
1. Installer MQTT Broker, MQTT Client og MQTT Hub på din Homey.
2. Opret user og password i brokeren
3. Opret forbindelse mellem broker og client.
4. Tast IP-adressen for brokeren ind i .yaml filen
5. Tast user og password ind i .yaml filen

## Filstruktur
Placer filerne i din ESPHome-mappe som følger:  
- ihc_controller.yaml (1-8 eller 9-16)  
- secrets.yaml  
- custom_component 
     - ihc_output  
         - __ __init__ __.py  
         - ihc_output.h  
         - ihc_output.cpp  

## Kompilering og Flash
1. Tilslut ESP32 via USB - verificere com-port i enhedshåndteringer.
2. Åben terminal og gå til mappen med filerne i (fx. c:\esphome\Ihc-output-bridge)
3. Kør i terminal: esphome run ihc_controller.yaml
4. Ved første flash: Hold BOOT-knappen nede når terminalen skriver "Connecting...". Slip efter ca. 3 sekunder - efterfølgende rettelser kan gøres via OTA

## Pin-konfiguration
- IHC Output Modul 1 -> GPIO 13
- IHC Output Modul 2 -> GPIO 14
- IHC Output Modul 3 -> GPIO 16
- IHC Output Modul 4 -> GPIO 17
- IHC Output Modul 5 -> GPIO 25
- IHC Output Modul 6 -> GPIO 26
- IHC Output Modul 7 -> GPIO 27
- IHC Output Modul 8 -> GPIO 32

## Integration med Homey Pro (via MQTT)
API integration via ESPHome-appen til Homey afventer opdatering (Q1 2026), indtil da kan MQTT benyttes.
1. Tilføj device via MQTT Hub.
2. Navngiv enheden og vælg device class "Wall plug"
3. Søg efter passende ikon
4. Tryk "+ add capability" og vælg "onoff - Turned on..."
5. Indsæt state topic "ihc_bridge/switch/ihcoutput_X_Y/state"
6. Indsæt set topic "ihc_bridge/switch/ihcoutput_X_Y/command"
7. Gem enheden og find den efterfølgende under enheder
8. Højre klik på enheden og vælg indstillinger
9. Ændre "on/off values" til on/off og gem

## MQTT Topics
Du kan styre enhederne manuelt via topics (X=modul, Y=udgang):
- Command: ihc_bridge/switch/ihcoutput_X_Y/command (Payload: ON/OFF).
- State: ihc_bridge/switch/ihcoutput_X_Y/state (Returværdi: ON/OFF).
