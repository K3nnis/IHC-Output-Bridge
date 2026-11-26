// Indlæser src/secrets.h, hvis den findes, og giver tomme standardværdier
// for alt, der ikke er sat. Se src/secrets.example.h.
#pragma once

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef IHC_SECRET_WIFI_SSID
#define IHC_SECRET_WIFI_SSID ""
#endif
#ifndef IHC_SECRET_WIFI_KODE
#define IHC_SECRET_WIFI_KODE ""
#endif
#ifndef IHC_SECRET_MQTT_VAERT
#define IHC_SECRET_MQTT_VAERT ""
#endif
#ifndef IHC_SECRET_MQTT_BRUGER
#define IHC_SECRET_MQTT_BRUGER ""
#endif
#ifndef IHC_SECRET_MQTT_KODE
#define IHC_SECRET_MQTT_KODE ""
#endif
