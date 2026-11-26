# Installation af firmwaren

Den nemmeste vej er **web-installeren**:
**https://k3nnis.github.io/IHC-Output-Bridge/**

Den kræver ingen programmer, kun Google Chrome eller Microsoft Edge på en computer. Siden indeholder samme vejledning som herunder.

## Det skal du bruge

- Et ESP32-S3-board med 16 MB flash og 8 MB PSRAM (**N16R8**), f.eks. ESP32-S3-DevKitC-1 eller YD-ESP32-S3.
- **Et USB-C-kabel, der kan overføre data.** Mange kabler kan kun lade. Boardet lyser fint med sådan et kabel, men computeren kan ikke se det.
- En computer med **Google Chrome** eller **Microsoft Edge**. Det virker ikke i Safari, Firefox eller på telefoner.

Boardet behøver ikke være forbundet til IHC-modulerne under installationen. Brug kun USB-kablet som strøm, og tag en eventuel ekstern 5 V-forsyning af imens.

## Trin 1: Tjek at computeren kan se boardet

Gør dette, før du installerer. Så ved du, at kablet og eventuel driver er i orden.

### Windows

1. Sæt kablet i boardets USB-C-port mærket **COM** og i computeren. Brug ikke en USB-hub eller dockingstation.
2. Højreklik på **Start**-knappen og vælg **Enhedshåndtering**.
3. Fold **Porte (COM og LPT)** ud.
4. Tag kablet ud og sæt det i igen, og se efter en port, der forsvinder og dukker op igen, f.eks. **USB-SERIAL CH343 (COM8)** eller **USB-seriel enhed (COM8)**. Notér nummeret.

**Dukker porten op, er alt klar.** Gå videre til trin 2.

### Der dukker ingen port op

Prøv disse ting i rækkefølge, og tjek Enhedshåndtering efter hver:

1. **Et andet kabel.** Det er langt den hyppigste årsag. Brug et kabel, du ved kan overføre data.
2. **Installér driveren til CH343-chippen**, som sidder ved porten mærket COM. Hent **CH343SER.EXE** fra producenten WCH: https://www.wch-ic.com/downloads/CH343SER_EXE.html. Kør filen, tryk **INSTALL**, og sæt boardet i igen bagefter.
3. **Brug den anden port, mærket USB.** Den går direkte til ESP32-S3'en og kræver ingen driver. Tag kablet ud, hold **BOOT**-knappen inde, sæt kablet i porten mærket **USB**, og slip **BOOT**. Der skal dukke en ny COM-port op og en enhed, der hedder **USB JTAG/serial debug unit**.

Står der en enhed med gul trekant under **Andre enheder**, mangler driveren (punkt 2). Ændrer intet sig i Enhedshåndtering, når kablet sættes i, er det kablet (punkt 1).

### Mac og Linux

Boardet virker normalt uden driver. Linux: din bruger skal have adgang til serielle porte (gruppen `dialout` på de fleste distributioner). Dukker boardet ikke op, så brug porten mærket **USB** som beskrevet ovenfor, punkt 3.

## Trin 2: Installér firmwaren

1. Åbn **https://k3nnis.github.io/IHC-Output-Bridge/** i Chrome eller Edge.
2. Lad boardet sidde i den port, du fandt i trin 1, og tryk **Installér IHC Bridge**.
3. Browseren viser en liste over porte. Vælg porten med det nummer, du noterede (f.eks. COM8), og tryk **Forbind**.
4. Installationsvinduet er på engelsk. Tryk **Install IHC Bridge**.
5. Ved **første installation**: sæt hak ved **Erase device**, så alt gammelt på boardet slettes. Tryk **Next** og derefter **Install**.
6. Vent, til der står **Installation complete!**. Det tager 1-2 minutter. Luk ikke fanen, og tag ikke kablet ud imens.
7. Tryk **Next**. Vinduet går tilbage til menuen; luk det med krydset. Installationen er færdig.

Menupunktet **Logs & Console** viser boardets log direkte i browseren. Der kan du f.eks. se, om IHC Bridge er kommet på dit Wi-Fi, og hvilken adresse den har fået.

> **Installerer du igen på et board, der allerede kører IHC Bridge?** Sæt *ikke* hak ved **Erase device**, så bevares Wi-Fi, adgangskode, navne og regler.

**Installationen kan ikke forbinde til boardet** ("Failed to connect with the device"): hold **BOOT** inde, tryk kort på **RST**, slip **BOOT**, og prøv igen. Luk også andre programmer, der bruger porten (seriel monitor, Arduino IDE).

## Trin 3: Sæt IHC Bridge op

1. Tryk på **RST** på boardet, hvis det ikke selv er genstartet efter installationen.
2. Forbind telefon eller computer til Wi-Fi'et **IHC-Bridge-Opsætning** (det er åbent).
3. Åbn **http://192.168.4.1** i en browser.
4. Vælg dit eget Wi-Fi, skriv Wi-Fi-koden, og vælg en adgangskode til IHC Bridge (mindst 8 tegn). Tryk **Gem og forbind**.
5. IHC Bridge forbinder nu selv til dit Wi-Fi. Skift telefonen eller computeren tilbage til dit eget Wi-Fi, og åbn **http://ihc-bridge.local**. Der logger du ind og kan se enhedens IP-adresse. Gem adressen som bogmærke.

Adressen **http://ihc-bridge.local** virker på Windows 10/11, Mac, iPhone, iPad og de fleste Android-telefoner. Virker den ikke på din enhed, så find IHC Bridge i routerens liste over tilsluttede enheder (den hedder **ihc-bridge**) og brug IP-adressen.

## Opdateringer senere

Når IHC Bridge først er installeret, opdateres den fra sit eget web-UI uden kabel:

1. Hent filen, der ender på **-ota.bin**, under [seneste version på GitHub](https://github.com/K3nnis/IHC-Output-Bridge/releases/latest).
2. Åbn IHC Bridge's web-UI, vælg **Firmware**, vælg filen og tryk **Opdatér firmware**.

Udgangene beholder deres tilstand. Går den nye firmware ned inden for det første minut, starter enheden igen med den forrige.

## Hvis noget går galt

| Problem | Løsning |
|---|---|
| Boardet lyser, men ingen port i Enhedshåndtering | Kablet kan kun lade. Prøv et andet |
| Gul trekant under Andre enheder | Installér CH343-driveren, eller brug porten mærket USB |
| Installationssiden viser en besked om browseren | Brug Chrome eller Edge på en computer |
| "Failed to connect with the device" | Hold BOOT, tryk RST, slip BOOT, og prøv igen |
| Porten er optaget | Luk andre programmer, der bruger porten |
| Wi-Fi'et IHC-Bridge-Opsætning dukker ikke op | Tryk på RST på boardet, og vent 10 sekunder |
| Du vil se, hvad boardet laver | Åbn installationssiden, tryk på knappen, vælg porten og **Logs & Console** |

---

## For udgiveren: sådan laves en ny version

Web-installeren og firmwarefilerne laves automatisk af GitHub Actions, når der oprettes et release.

**Engangsopsætning** i repoet på GitHub:

1. **Settings → Pages → Build and deployment → Source:** vælg **GitHub Actions**.
2. **Settings → Environments → github-pages → Deployment branches and tags:** tryk **Add deployment branch or tag rule**, vælg **Tag**, skriv `v*`, og tryk **Add rule**. Uden den regel må web-installeren kun udgives fra `main`, ikke fra et release-tag.

**Ny version:**

1. Sørg for, at ændringerne er flettet ind i `main`.
2. På GitHub: **Releases → Draft a new release**.
3. Under **Choose a tag** skriv et nyt versionsnummer med `v` foran, f.eks. `v2.0.0`, og vælg **Create new tag on publish**. Target: `main`.
4. Tryk **Publish release**.

En version med bindestreg, f.eks. `v2.0.0-beta.1`, markeres som testversion (pre-release). Til test før koden er flettet ind i `main` kan **Target** sættes til arbejdsgrenen.

GitHub Actions bygger og tester firmwaren, sætter versionsnummeret ind, lægger `ihc-bridge-<version>-fuld.bin` og `ihc-bridge-<version>-ota.bin` på releasen og opdaterer web-installeren. Det tager 5-10 minutter; fremdriften ses under **Actions**.

Installeren kan også bygges lokalt:

```bash
pio run -e esp32s3
```
```bash
python3 tools/lav_installer.py --version 2.0.0 --ud site
```
