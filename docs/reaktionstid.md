# Reaktionstid: fra tryk til tændt udgang

**Krav:** under 500 ms fra et tryk på en knap (IHC-input) til udgangen tænder.
**Resultat (simuleret):** typisk 65-70 ms, værst 89 ms. Det er under en femtedel af kravet.

## Hvor tiden går

| Trin | Tid | Kommentar |
|---|---|---|
| Inputmodulet aflæser knappen | 0-19 ms | Modulet sender en frame ca. hvert 19. ms |
| Frame sendes fra inputmodulet | ~11 ms | 17 bit à 624 µs |
| Bekræftelse: to ens frames | +19 ms | Beskytter mod støj på ledningen |
| **Regel udføres** | **< 0,01 ms** | Direkte i bus-interruptet, se nedenfor |
| Venter på næste output-frame | 0-19 ms | Data låses lige før databittene |
| Output-frame sendes | ~11 ms | Outputmodulet skifter, når frame'n er modtaget |
| Relæets egen skiftetid | ~5-15 ms | Mekanisk, ikke med i simulationen |

## Designvalg, der holder tiden nede

1. **Reglerne kører i selve bus-interruptet.** Når Modtager har bekræftet et
   tryk, slår interruptet op i en færdigbygget regeltabel (128 indgange × 128
   udgange) og kipper udgangen med det samme. Der er ingen FreeRTOS-task,
   ingen kø og intet netværk imellem.
2. **Intet i reaktionskæden ligger i flash.** Hele interrupt-kæden ligger i
   intern RAM (IRAM). Den kører derfor også, mens der skrives til flash (gemt
   tilstand, firmwareopdatering). `tools/check_isr_iram.sh` kontrollerer det
   automatisk i den færdige firmware.
3. **Data til outputmodulerne låses så sent som muligt**, lige før databittene
   og ikke ved frame-start. Det sparer ca. 8 ms.
4. **Alt andet sker bagefter og på den anden kerne.** Logning, gemning i flash
   og senere MQTT og web får besked, når udgangen allerede er sat.

## Sådan er det målt

Testen `test_reaktionstid` i `test/test_styring/test_main.cpp` simulerer hele
kæden med samme 39 µs opløsning som interruptet: et inputmodul, der sender
frames, Modtager, regeltabellen, Styring og Sender. Tiden måles fra tryk til
outputmodulet har modtaget en hel frame med udgangen tændt.

- 300 tryk på tilfældige tidspunkter og med tilfældig fase mellem inputmodul og bus
- Inputmodulets takt varieret 95 %, 100 % og 105 %
- Inputmodulet antages at aflæse knappen ved starten af hver frame (værste fald)

| Inputmodulets takt | Gennemsnit | Værst |
|---|---|---|
| 95 % | 65 ms | 84 ms |
| 100 % | 67 ms | 85 ms |
| 105 % | 70 ms | 89 ms |

Testen fejler, hvis det værste tilfælde overstiger 100 ms, så en senere
ændring, der gør reaktionen langsommere, bliver opdaget med det samme.

## Kan det gøres hurtigere?

Den største enkeltpost er bekræftelsen med to ens frames (19 ms). Den kunne
fjernes, men så kan en enkelt forstyrrelse på ledningen kippe en udgang. Med
god margin til kravet er sikkerheden vigtigere.
