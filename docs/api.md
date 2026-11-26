# Web-API

Alle svar er JSON. Fejl har formen `{"ok": false, "fejl": "Forklaring på dansk"}`.
POST-forespørgsler skal sendes med `Content-Type: application/json`.

Moduler og kanaler tælles fra 1, som på IHC-modulerne.

## Login

Login giver en cookie (`ihc_session`, HttpOnly, SameSite=Strict), som browseren
sender med automatisk. En session udløber efter 7 dages inaktivitet og ved
genstart af enheden. Efter 5 forkerte forsøg skal der ventes 30 sekunder, og
ventetiden fordobles for hvert nyt forkert forsøg (højst 15 minutter).

| Metode | Sti | Login | Beskrivelse |
|---|---|---|---|
| GET | `/api/opsaetning` | nej | `{"opsat", "version", "wifi": {...}}` |
| POST | `/api/opsaetning` | nej* | Første opsætning: `{"kode", "wifi_ssid", "wifi_kode"}` |
| GET | `/api/wifi/scan` | nej* | `{"netvaerk": [{"ssid", "rssi", "krypteret"}]}` |
| POST | `/api/login` | nej | `{"kode"}` |
| POST | `/api/logud` | - | Logger denne browser ud |
| POST | `/api/kode` | ja | Skift adgangskode: `{"gammel", "ny"}`. Alle andre logges ud |

\* Kun før enheden er sat op. Bagefter kræver scanning login, og
opsætning afvises med `409`.

`wifi_ssid` tom i `POST /api/opsaetning` betyder "behold det gemte netværk"
(bruges efter nulstilling med BOOT-knappen).

## Status og styring

| Metode | Sti | Beskrivelse |
|---|---|---|
| GET | `/api/status` | System, Wi-Fi, MQTT og bus-diagnose |
| GET | `/api/io` | Alle moduler med tilstand, navne og typer |
| POST | `/api/udgang` | `{"modul": 2, "kanal": 2, "tilstand": true \| false \| "skift"}` |
| POST | `/api/kanal` | Navn og type: `{"retning": "udgang" \| "indgang", "modul", "kanal", "navn", "type"}` |
| POST | `/api/moduler` | Aktive moduler: `{"udgange": [1, 2, 5], "indgange": [1, 2]}` |
| GET | `/api/regler` | `{"regler": [{"type": "kip", "ind_modul", "ind_kanal", "ud_modul", "ud_kanal"}]}` |
| POST | `/api/regler` | Samme format. Erstatter alle regler og aktiveres med det samme |

`tilstand` i `/api/io` er et tal, hvor bit 0 er kanal 1. For outputmodul 2 med
udgang 1 og 3 tændt er `tilstand` = 5.

Navne må højst være 32 bytes. Et tomt navn giver standardnavnet
("Udgang 2.2" / "Indgang 3.15").

| Udgangstype | Home Assistant |
|---|---|
| `lys` (standard) | `light` |
| `stikkontakt` | `switch`, device class `outlet` |
| `kontakt` | `switch` |
| `ventilator` | `fan` |

| Indgangstype | Home Assistant |
|---|---|
| `tryk` (standard) | `binary_sensor` |
| `doer` | `binary_sensor`, device class `door` |
| `vindue` | `binary_sensor`, device class `window` |
| `bevaegelse` | `binary_sensor`, device class `motion` |

## Indstillinger

| Metode | Sti | Beskrivelse |
|---|---|---|
| GET/POST | `/api/wifi` | `{"ssid", "kode", "vaertsnavn", "fast_ip", "ip", "maske", "gateway", "dns"}` |
| GET/POST | `/api/mqtt` | `{"vaert", "port", "bruger", "kode", "klient_id", "basis_topic", "payload_on", "payload_off", "retain", "qos", "keepalive", "discovery", "discovery_prefix"}` |

Koder sendes aldrig tilbage. GET på `/api/mqtt` viser `"har_kode": true/false`.
Udelades `kode` i en POST, beholdes den gemte kode. Felter, der udelades,
beholder deres værdi.

## Firmware

| Metode | Sti | Beskrivelse |
|---|---|---|
| POST | `/api/firmware` | Body: `firmware.bin` (rå binær). Genstarter bagefter |
| POST | `/api/genstart` | Genstarter enheden |

Filen tjekkes, før der skrives: den skal være firmware til IHC Bridge, og
kontrolsummen skal passe. Den nye firmware bekræftes, når den har kørt i 60
sekunder. Går den ned inden, starter enheden igen med den forrige firmware.

## WebSocket: `/ws`

Kræver login (cookien sendes med). Serveren sender ændringer, så snart de
sker, og hele tilstanden, når forbindelsen åbnes:

```json
{"type": "tilstand",
 "udgange": {"2": 5},
 "indgange": {"1": {"online": true, "tilstand": 129}}}
```

Kun ændrede moduler er med. Klienten kan styre udgange over samme forbindelse:

```json
{"type": "udgang", "modul": 2, "kanal": 2, "tilstand": "skift"}
```
