# Test af MQTT og Home Assistant (fase 5)

Indtil web-UI'et er klar, kan Wi-Fi og MQTT sættes op med `src/secrets.h`.

## 1. Opsætning med secrets.h

1. Kopiér `src/secrets.example.h` til `src/secrets.h`.
2. Udfyld Wi-Fi og MQTT-broker (f.eks. Mosquitto-add-on'en i Home Assistant):
   ```c
   #define IHC_SECRET_WIFI_SSID "MitNetværk"
   #define IHC_SECRET_WIFI_KODE "min-wifi-kode"
   #define IHC_SECRET_MQTT_VAERT "192.168.1.10"
   #define IHC_SECRET_MQTT_BRUGER "mqtt-bruger"
   #define IHC_SECRET_MQTT_KODE "mqtt-kode"
   ```
   `src/secrets.h` er udelukket fra git og kommer aldrig på GitHub.
3. Flash og åbn den serielle monitor:
   ```bash
   pio run -e esp32s3 -t upload
   ```
   ```bash
   pio device monitor -e esp32s3
   ```
   Loggen skal vise `Forbundet med IP ...` og `Forbundet til broker ...`.

## 2. Topics

| Topic | Indhold |
|---|---|
| `ihc/ihc-bridge/status` | `online` / `offline` (last will) |
| `ihc/ihc-bridge/output/2/2/state` | `ON` / `OFF` |
| `ihc/ihc-bridge/output/2/2/set` | send `ON`, `OFF` eller `TOGGLE` |
| `ihc/ihc-bridge/input/3/15/state` | `ON` / `OFF` |
| `ihc/ihc-bridge/input/3/status` | `online` / `offline` for inputmodul 3 |

Se alt, bridgen sender (kræver `mosquitto-clients`):
```bash
mosquitto_sub -h 192.168.1.10 -u mqtt-bruger -P mqtt-kode -v -t 'ihc/#'
```

Tænd udgang 2.2:
```bash
mosquitto_pub -h 192.168.1.10 -u mqtt-bruger -P mqtt-kode -t 'ihc/ihc-bridge/output/2/2/set' -m ON
```

## 3. Home Assistant

Med discovery slået til (standard) dukker enheden **IHC Bridge** op under
*Indstillinger → Enheder og tjenester → MQTT*. Udgange er kontakter
(`switch`), indgange er binære sensorer (`binary_sensor`).

Indtil modulvalg kommer i web-UI'et, er alle 16 outputmoduler og 8
inputmoduler aktive (128 udgange og 128 indgange). Inputmoduler, der ikke
er tilsluttet, vises som *utilgængelige*.

## Det skal virke

- [ ] `status` bliver `online` ved opstart og `offline`, når strømmen fjernes
- [ ] `set` med `ON`/`OFF`/`TOGGLE` skifter udgangen, og `state` følger med
- [ ] Et tryk på en IHC-knap giver `ON`, og slip giver `OFF` på `input/.../state`
- [ ] En lokal regel (tryk på knap) opdaterer også udgangens `state`
- [ ] Efter genstart af Home Assistant vises alle tilstande korrekt igen
