# Test af opsætning, login og firmwareopdatering (fase 6)

> **Partitionstabellen er ændret i fase 6.** Første gang skal firmwaren derfor
> flashes via USB (COM-porten), ikke fra web-UI'et.

## 1. Førstegangsopsætning

1. Flash via USB:
   ```bash
   pio run -e esp32s3 -t upload
   ```
2. Forbind telefon eller computer til Wi-Fi'et **IHC-Bridge-Opsætning** (åbent).
3. Åbn **http://192.168.4.1** i en browser.
4. Tryk **Søg**, vælg dit netværk, skriv Wi-Fi-koden, og vælg en adgangskode
   til IHC Bridge (mindst 8 tegn).
5. Tryk **Gem og forbind**. Siden viser enhedens nye adresse, når den er på dit
   netværk. Skift tilbage til dit eget Wi-Fi og åbn adressen.

Opsætnings-access-point'et lukker et minut efter, at forbindelsen virker.
Er Wi-Fi-koden forkert, forbliver det åbent, så du kan prøve igen.

## 2. Login og oversigt

- Åbn enhedens adresse. Du er allerede logget ind lige efter opsætningen.
- Slå de tilsluttede outputmoduler til under **Moduler og navne**.
- Inputmoduler dukker op af sig selv, når de sender.
- Et klik på en udgang skifter den. Tryk på IHC-knapper vises live.

## 3. Glemt adgangskode

1. Hold **BOOT**-knappen inde i **10 sekunder**, mens enheden kører.
   (Hold den *ikke* inde, mens strømmen tilsluttes: så starter ESP32'en i
   flash-tilstand i stedet.)
2. Loggen skriver `Adgangskoden er nulstillet`. Alle er logget ud.
3. Åbn enhedens adresse og vælg en ny adgangskode. Lad Wi-Fi-felterne være
   tomme for at beholde netværket. Wi-Fi, MQTT, navne og regler bevares.

## 4. Firmwareopdatering fra web-UI'et

1. Byg firmwaren:
   ```bash
   pio run -e esp32s3
   ```
2. Åbn **Firmware** i web-UI'et, vælg `.pio/build/esp32s3/firmware.bin` og tryk
   **Opdatér firmware**.

Enheden genstarter med den nye firmware. `/api/status` viser
`"firmware_afventer_bekraeftelse": true` det første minut. Går enheden ned
inden, starter den med den forrige firmware.

## Det skal virke

- [ ] Opsætnings-AP'et dukker op ved første opstart, og siden kan scanne netværk
- [ ] Efter opsætning viser siden den nye IP-adresse
- [ ] Forkert adgangskode afvises; efter 5 forsøg skal der ventes
- [ ] Udgange kan styres fra siden, og IHC-tryk vises live
- [ ] BOOT i 10 s nulstiller adgangskoden, og en ny kan vælges
- [ ] Firmware kan opdateres fra web-UI'et
