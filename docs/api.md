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
| `temperatur` | IHC-temperaturføler: `sensor` for temperatur (og fugt/gulv). Se `docs/temperatur.md` |

For indgange af typen `temperatur` indeholder kanalen i `/api/io` også
`"temperatur": {"online", "temperatur", "fugt", "gulv", "alder_s"}` (fugt og
gulv kun, hvis føleren sender dem), eller `null`, før der er modtaget noget.

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
| GET | `/api/opdatering` | Status for online-opdatering, se nedenfor |
| POST | `/api/opdatering/soeg` | Søger efter ny version på GitHub (i baggrunden) |
| POST | `/api/opdatering/installer` | `{"version":"2.0.0"}` Installerer den fundne version. Versionen skal passe med den fundne |
| POST | `/api/opdatering/indstillinger` | `{"testversioner":true}` Tag testversioner (pre-releases) med |

Filen tjekkes, før der skrives: den skal være firmware til IHC Bridge, og
kontrolsummen skal passe. Den nye firmware bekræftes, når den har kørt i 60
sekunder. Går den ned inden, starter enheden igen med den forrige firmware.

### Online-opdatering

Enheden henter selv listen over releases fra GitHubs API og vælger den nyeste
release med en `-ota.bin`-fil fra repoet i `kRepo` (`src/opdatering.h`).
Filen hentes over HTTPS direkte fra repoets releases og tjekkes som ved
manuel opdatering.

`GET /api/opdatering`:

```json
{"tilstand":"fundet","installeret":"2.0.0-beta.1","fejl":"","procent":0,
 "testversioner":false,"sidst_soegt_s":12,
 "release":{"version":"2.0.0","navn":"IHC Bridge 2.0.0","dato":"2026-12-01",
            "side":"https://github.com/…/releases/tag/v2.0.0","noter":"…",
            "stoerrelse":1263778,"test":false}}
```

`tilstand` er `klar` (ikke søgt), `soeger`, `nyeste` (installeret er nyest),
`fundet` (nyere version klar), `henter` (`procent` viser fremdriften),
`genstarter` eller `fejl` (se `fejl`). `release` er med, når en release er
fundet. Under søgning og installation spørger web-UI'et hvert sekund.

## Sikkerhedskopi og log

| Metode | Sti | Beskrivelse |
|---|---|---|
| GET | `/api/eksport` | Moduler, navne, typer, regler og MQTT som JSON-fil (uden koder og Wi-Fi) |
| POST | `/api/import` | Samme format. Alt kontrolleres, før noget ændres; MQTT-koden i enheden bevares |
| GET | `/api/log?efter=N` | Loglinjer med løbenummer over N: `{"linjer": [{"nr", "tekst"}], "sidste"}` |

## WebSocket: `/ws`

Kræver login (cookien sendes med). Serveren sender ændringer, så snart de
sker, og hele tilstanden, når forbindelsen åbnes:

```json
{"type": "tilstand",
 "udgange": {"2": 5},
 "indgange": {"1": {"online": true, "tilstand": 129}}}
```

Kun ændrede moduler er med. Ved nye temperaturmålinger sendes
`"temperaturer": {"1.7": {...}}` i samme format som i `/api/io`. Klienten kan styre udgange over samme forbindelse:

```json
{"type": "udgang", "modul": 2, "kanal": 2, "tilstand": "skift"}
```
