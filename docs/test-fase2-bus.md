# Test af IHC-bussen (fase 2) med logic analyzer

Denne tjekliste verificerer, at ESP32-S3 sender korrekte IHC-signaler til hvert
outputmodul og modtager korrekt fra inputmodulerne.

## Det skal du bruge

- VS Code med udvidelsen **PlatformIO IDE**
- Logic analyzer, der tåler 5 V på indgangene, og som kan sample med mindst 1 MHz
  (f.eks. PulseView/sigrok eller Saleae Logic)
- ESP32-S3-boardet tilsluttet via **COM**-porten (USB-C ved CH343P-chippen)

> **ADVARSEL:** Testfirmwaren tænder og slukker udgange efter et fast mønster.
> Lav test 1 på et prøvebord eller med relæernes belastning afbrudt.

## Test 1: Outputsignaler (testfirmware)

1. Flash testfirmwaren:
   ```bash
   pio run -e esp32s3_bustest -t upload
   ```
2. Sæt logic analyzeren på **output-siden af 74HCT541** (Y-benene), så du måler
   5 V-signalet, som modulerne ser. Mål gerne 4-8 linjer på én gang.
3. Hvert outputmodul sender sit eget modulnummer binært, så linjerne kan
   skelnes fra hinanden:

   | Modul | GPIO | Udgange der er ON | Udgang 1-8 (bit) | Paritetsbit |
   |---|---|---|---|---|
   | 1 | 1 | 1 | `1000 0000` | 0 (lang høj) |
   | 2 | 2 | 2 | `0100 0000` | 0 (lang høj) |
   | 3 | 4 | 1, 2 | `1100 0000` | 1 (kort høj) |
   | 4 | 5 | 3 | `0010 0000` | 0 (lang høj) |
   | 8 | 9 | 4 | `0001 0000` | 0 (lang høj) |
   | 16 | 17 | 5 | `0000 1000` | 0 (lang høj) |

   Udgang 1 sendes først. Bit 9-16 er altid OFF.

4. **Forventet timing** (samme på alle linjer):

   | Del | Niveau | Varighed |
   |---|---|---|
   | Pause | lav | 4,06 ms |
   | Startpuls | høj | 4,37 ms |
   | Hver bit, start | lav | 312 µs |
   | Bit = ON | høj, så lav | 156 µs + 156 µs |
   | Bit = OFF | høj | 312 µs |
   | Hel frame | | 19,03 ms (17 bit) |

   Afvigelser på et par µs er normale. Mere end ca. 20 µs bør meldes tilbage.

5. **Sammenlign med den originale controller**, hvis du har en måling derfra:
   pause, startpuls og bit-timing bør ligne hinanden.

## Test 2: Inputmoduler (almindelig firmware)

1. Flash den almindelige firmware og åbn den serielle monitor:
   ```bash
   pio run -e esp32s3 -t upload
   ```
   ```bash
   pio device monitor -e esp32s3
   ```
2. Forbind inputmodul 1 via 74LVC245 til GPIO 18 (modul 2 = GPIO 21, se
   `src/board_pins.h`). Loggen skal skrive `Inputmodul 1 er online`.
3. Tryk på tryk/kontakter. Hver ændring logges, f.eks. `Input 1.5: ON`.
4. Hvert 10. sekund skrives en statuslinje pr. inputmodul:
   ```
   Inputmodul 1: online, tilstand 0x0010, ok 523, paritet16 523, paritet8 61, paritetsfejl 0, pulsfejl 0
   ```
   **Vigtigt:** Tryk på nogle indgange i området **9-16**, og se, om det er
   `paritet16` eller `paritet8`, der følger med `ok`. Det afgør, hvilken
   paritetsregel inputmodulerne bruger. Meld tallene tilbage.
5. `pulsfejl` og `paritetsfejl` bør være 0 eller meget tæt på 0.

## Det skal meldes tilbage

- Om timingen i test 1 passer (gerne et skærmbillede af én frame)
- Om modulerne reagerer på testmønstret
- Statuslinjerne fra test 2, især `paritet16`/`paritet8`
- `max ISR-tid` fra statuslinjen (forventet under ca. 10 µs)
