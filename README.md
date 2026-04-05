# IHC Output Bridge (ESPHome & ESP32)

Dette projekt gør det muligt at udskifte en traditionel LK IHC Controller med en moderne **ESP32 Wroom**, hvor du genbruger dine eksisterende IHC Output-moduler (f.eks. 230V relæmoduler). 

Denne bridge gør det muligt at styre op til **16 IHC Output moduler (128 udgange)** ved hjælp af en ESP32. Kommunikationen foregår enten via MQTT, POST, ESPhome APP, hvilket sikrer integration med Homey Pro, Home Assistant og andre platforme.

## Funktioner
* **Emulering** af IHC-protokol mellem IHC controller og IHC Output Moduler.
* **100 % Modulært:** Koden er opsat i smarte `packages`. Du kan styre op til **16 IHC Output moduler (128 udgange)**. Slå moduler til og fra ved blot at fjerne et `#` i konfigurationsfilen.
* **Webinterface:** Indbygget webserver til manuel test af alle udgange.
* **Universel Integration:** Styr det hele via MQTT.

---

## Hardware Krav

For at sikre et stabilt signal til IHC-modulerne, er det ikke nok at forbinde ESP32'eren direkte, da den kun udsender 3.3V med meget lav strømstyrke. Derfor skal der bruges en **Buffer IC**.

* **1x ESP32 Wroom** (Udviklingsboard)
* **2 stk. sN74HCT541 (DIP-20)**. *Vigtigt: Bogstavet "T" er altafgørende, da det sikrer, at chippen oversætter ESP32'erens 3.3V til et stærkt 5V TTL-signal.*
* **Genbrug 24v IHC Strømforsyning** ved at sætte et step down modul på fra ***24V til 5V*** til forsyning af ESP32 og Buffer-chip.
* **Hulprint (Perfboard), IC-sokler og skrueterminaler** til montering.
* **Fælles GND** ***er meget vigtigt*** for stabilt signal på alle GPIO udgange.

### Forbindelser (Pinout)
Chippen `74HCT541` er utrolig nem at trække ledninger til. A-siden (Input) sættes til ESP32, og Y-siden (Output) sættes til IHC-modulets data-indgang.

**Montering af 74HCT541:**
* Pin 20 (VCC) -> 5V
* Pin 10 (GND) -> GND (Fælles GND for alle!)
* Pin 1 (OE1) & Pin 19 (OE2) -> GND (Aktiverer udgangene permanent)

**Dataledninger for Chip 1 (IHC Modul 1-8):**
| Fra ESP32 Pin | 74HCT541 Input | 74HCT541 Output | Til IHC Modul |
| :--- | :--- | :--- | :--- |
| GPIO 13 | Pin 2 (A1) | Pin 18 (Y1) | Modul 1, Data |
| GPIO 14 | Pin 3 (A2) | Pin 17 (Y2) | Modul 2, Data |
| GPIO 16 | Pin 4 (A3) | Pin 16 (Y3) | Modul 3, Data |
| GPIO 17 | Pin 5 (A4) | Pin 15 (Y4) | Modul 4, Data |
| GPIO 25 | Pin 6 (A5) | Pin 14 (Y5) | Modul 5, Data |
| GPIO 26 | Pin 7 (A6) | Pin 13 (Y6) | Modul 6, Data |
| GPIO 27 | Pin 8 (A7) | Pin 12 (Y7) | Modul 7, Data |
| GPIO 32 | Pin 9 (A8) | Pin 11 (Y8) | Modul 8, Data |

*(Se YAML-filerne for pin-konfiguration til Chip 2 / Modul 9-16).*

---
## Installation
1. Download eller klon dette repository.
2. Læg filerne i din ESPHome-mappe.
3. Installer MQTT Broker, MQTT Client og MQTT Hub på din Homey.
4. Opret user og password i brokeren
5. Opret forbindelse mellem broker og client.
6. Tilslut ESP32 via USB - verificere com-port i enhedshåndteringer.
7. Åbn `ihc_controller.yaml` og ændrer dine MQTT-oplysninger.
8. Rul ned i bunden af `ihc_controller.yaml` til `packages:` sektionen. Sæt et `#` foran de moduler, du *ikke* har tilsluttet endnu.
9. Ret oplysninger i `secrets.yaml`.
10. Åben terminal og kør ESPHome.
11. Gå til mappen med filerne i (fx. c:\esphome\Ihc-output-bridge) og kompilér koden:
    ```bash
       esphome run ihc_controller.yaml
12. Ved første flash: Hold BOOT-knappen nede når terminalen skriver "Connecting...". Slip efter ca. 3 sekunder - efterfølgende flashing kan gøres via OTA.

## Integration med Homey Pro (via MQTT)
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

## Integration med Homey Pro (via POST)
1. Tilføj ny virtual switch
2. Opret et Flow, hvor din trigger **(NÅR)** er trykket på din switch.
3. I handlingen **(SÅ)** vælger du Homeys indbyggede Logik **(Logic)** kategori.
4. Vælg kortet "Lav en HTTP...".
5. **Metode:** Vælg POST (Meget vigtigt, ESPHome accepterer kun POST for at ændre tilstand).
6. **URL:** Indsæt URL'en (f.eks. http://192.168.1.50/switch/ihcoutput_1_1/turn_on).
7. Sidehoveder og brødtekst skal ***ikke*** udfyldes

**POST valgmuligheder:**  
/switch/ihcoutput_1_1/turn_on  
/switch/ihcoutput_1_1/turn_off  
/switch/ihcoutput_1_1/toggle  

## Integration med Homey Pro (via ESPHome Controller app)
1. Gå ind i Homey App Store og søg på "ESPHome".
2. Installer appen.
3. Tilføj device via ESPHome Controller.
4. Vælg ESPHome Switch og tryk forbind.
5. Angiv Device name "IHC".
6. Tast IP-adresse til IHC Output Bridge.
7. Tast API password eller encryption key.
8. Tryk Add Device.
9. Tryk Add Selected og alle IHC outputs tilføjes automatisk.
