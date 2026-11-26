// Eksempel på secrets.h: standardværdier, der compileres ind i firmwaren.
//
// Kopiér filen til src/secrets.h og udfyld det, du vil bruge. Tomme eller
// udeladte værdier ignoreres. secrets.h er udelukket fra git (.gitignore),
// så dine koder aldrig kommer på GitHub.
//
// Værdierne bruges KUN, når der ikke er gemt noget på enheden. Alt, du
// senere gemmer i web-UI'et, har forrang.
//
// Bemærk: Koderne ender i klartekst i firmwaren og i boardets flash. Filen
// gør det nemt at komme i gang, men er ikke mere sikker end web-UI'et.
#pragma once

// Wi-Fi
#define IHC_SECRET_WIFI_SSID ""
#define IHC_SECRET_WIFI_KODE ""

// MQTT-broker (f.eks. Mosquitto i Home Assistant)
#define IHC_SECRET_MQTT_VAERT ""  // IP-adresse eller værtsnavn, f.eks. "192.168.1.10"
#define IHC_SECRET_MQTT_BRUGER ""
#define IHC_SECRET_MQTT_KODE ""
