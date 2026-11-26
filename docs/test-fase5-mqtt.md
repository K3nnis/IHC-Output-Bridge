# Test af MQTT og Home Assistant (fase 5)

## 1. Opsætning

1. Sæt enheden op som beskrevet i `docs/test-fase6-web.md`.
2. Log ind, åbn **Wi-Fi og MQTT**, udfyld broker, brugernavn og kode, og tryk **Gem MQTT**.
3. Statuslinjen øverst viser **MQTT forbundet**, når forbindelsen virker.

## 2. Topics

| Topic | Indhold |
|---|---|
| `ihc/ihc-bridge/status` | `online` / `offline` (last will) |
| `ihc/ihc-bridge/output/2/2/state` | `ON` / `OFF` |
| `ihc/ihc-bridge/output/2/2/set` | send `ON`, `OFF` eller `TOGGLE` |
| `ihc/ihc-bridge/input/3/15/state` | `ON` / `OFF` |
| `ihc/ihc-bridge/input/3/status` | `online` / `offline` for inputmodul 3 |
| `ihc/ihc-bridge/temp/1/7/temperature` | `21.5`, temperaturføler på indgang 1.7 (se `docs/temperatur.md`) |

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
*Indstillinger → Enheder og tjenester → MQTT*.

Kun aktive moduler vises. Outputmoduler slås til i web-UI'et; inputmoduler
slås til automatisk, første gang de sender. Udgangenes type (lys,
stikkontakt, kontakt, ventilator) og indgangenes type (tryk, dør, vindue,
bevægelse) vælges i web-UI'et og bestemmer, hvordan de vises i Home Assistant.

## Det skal virke

- [ ] `status` bliver `online` ved opstart og `offline`, når strømmen fjernes
- [ ] `set` med `ON`/`OFF`/`TOGGLE` skifter udgangen, og `state` følger med
- [ ] Et tryk på en IHC-knap giver `ON`, og slip giver `OFF` på `input/.../state`
- [ ] En lokal regel (tryk på knap) opdaterer også udgangens `state`
- [ ] Efter genstart af Home Assistant vises alle tilstande korrekt igen
