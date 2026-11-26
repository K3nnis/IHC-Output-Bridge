# IHC-temperaturfølere

En IHC-temperaturføler sidder på en almindelig indgang på et inputmodul. Den
sender sine målinger ved at tænde og slukke indgangen i et langsomt mønster.
IHC Bridge afkoder mønstret og viser temperaturen i web-UI'et og i Home
Assistant.

## Sådan bruges en føler

1. Åbn **Moduler og navne** i web-UI'et.
2. Find den indgang, føleren sidder på, og vælg typen **Temperaturføler**.
   Giv den gerne et navn, f.eks. *Stue*.
3. Efter 5-15 sekunder vises temperaturen i oversigten.

En temperaturføler udløser aldrig regler, og dens ON/OFF-signal sendes ikke
til MQTT. Under **Diagnose og log** ses, hvor mange målinger der er modtaget,
og hvornår den seneste kom.

## MQTT og Home Assistant

| Topic | Indhold |
|---|---|
| `ihc/ihc-bridge/temp/1/7/temperature` | `21.5` (°C) |
| `ihc/ihc-bridge/temp/1/7/humidity` | `45.0` (%), kun følere med fugtmåler |
| `ihc/ihc-bridge/temp/1/7/floor` | `19.2` (°C), kun følere med gulvføler |
| `ihc/ihc-bridge/temp/1/7/status` | `online` / `offline` (ingen måling i 10 minutter) |

Med discovery slået til får Home Assistant en sensor for temperaturen og,
når føleren har sendt dem, også for luftfugtighed og gulvtemperatur.

## Signalet

| | Værdi |
|---|---|
| Bitlængde | ca. 122 ms |
| Bit "1" | tændt ca. 81 ms, slukket ca. 41 ms |
| Bit "0" | tændt ca. 41 ms, slukket ca. 81 ms |
| Pakke | 41 bit, mest betydende bit først |

Pakkens indhold:

| Bit | Indhold |
|---|---|
| 12 | rumtemperatur i tiendedele °C |
| 12 | luftfugtighed i tiendedele % (0 = ingen fugtmåler) |
| 12 | gulvtemperatur i tiendedele °C (0 = ingen gulvføler) |
| 5 | kontrolsum |

**Kontrolsum:** De tre værdier deles i ni grupper à 4 bit, som lægges sammen.
De laveste 4 bit af summen er kontrolsummen, og den 5. bit er 0. Uden
gulvføler sættes den 5. bit, og 4. bit vendes.

Eksempel: 21,5 °C, ingen fugt, gulv 19,2 °C giver værdierne 215, 0 og 192.
Grupperne 1101 + 0111 + 1100 = 32, og de laveste 4 bit er 0000. Kontrolsummen
er 00000.

## Sådan afkodes signalet

Inputmodulet sender indgangenes tilstand ca. hvert 19. ms, så bridgen ser
følerens pulser med 19 ms opløsning. Det gør afkodningen mere krævende end
selve signalet:

- **Hver frame bruges.** For almindelige indgange kræver bridgen to ens
  frames, før en ændring godtages. Det gælder ikke temperaturfølere, fordi
  korte pulser (41 ms) kan vare under to frames. Pariteten på hver frame og
  pakkens kontrolsum beskytter i stedet.
- **Følerens takt følges.** Starten af hver bit forudsiges ud fra følerens
  faste takt, og grænsen mellem kort og lang puls er halvdelen af den målte
  bitlængde. Så afkodes følere, der går fra 20 % for hurtigt til 30 % for
  langsomt.
- **To pakker i træk.** Efter opstart, eller når føleren har været væk,
  godtages en måling først, når to pakker i træk er gyldige og ligner hinanden
  (højst 5 °C forskel). Det koster én pakke (ca. 5 sekunder) ved opstart,
  men gør, at støj aldrig giver en falsk temperatur.
- **Realistiske værdier.** Temperaturer skal ligge mellem -50 og 100 °C og
  luftfugtighed mellem 0 og 100 %.

Testene i `test/test_temperatur` dækker bl.a. 200.000 tilfældige bit (ingen
falske målinger), pakker med og uden pause imellem, start midt i en pakke og
følere med afvigende takt.

## Ikke afklaret endnu

Afkoderen er skrevet ud fra protokolbeskrivelsen og testet med syntetiske
signaler, men ikke med en rigtig føler. Følgende antagelser bør tjekkes med en
logic analyzer, når en føler er tilgængelig:

- **Negative temperaturer** tolkes som 12 bit med fortegn (to-komplement).
  Det er ikke bekræftet, at følerne sender sådan.
- **Pauser mellem pakker** håndteres, men afkoderen virker også, hvis føleren
  sender uden pause.
- **Følere med lux-måler** sender efter en beskrivelse 12 bit temperatur, 12
  bit med en "skumrings"-bit og 12 bit lux. De vises i dag som temperatur
  med luftfugtighed og er ikke understøttet.
