// Temperatur: afkoder IHC-temperaturfølere på de indgange, der har typen
// "temperatur". Kører i en task på kerne 0 med data fra bus-interruptet.
//
// Moduler og kanaler tælles fra 1.
#pragma once

#include <cstdint>

#include "ihc_temperatur.h"

namespace temperatur {

// En føler regnes for offline, hvis der ikke er kommet en gyldig måling i
// denne periode.
constexpr uint32_t kOfflineEfterS = 600;

struct Foeler {
  bool har_maaling = false;
  ihc::TemperaturMaaling maaling;
  uint32_t alder_s = 0;   // sekunder siden seneste måling
  bool online = false;
  bool set_fugt = false;  // føleren har sendt luftfugtighed
  bool set_gulv = false;  // føleren har sendt gulvtemperatur
  uint32_t gyldige = 0;
  uint32_t fejl = 0;
};

void start();

// Kaldes, når en indgangs type er ændret.
void kanaler_aendret();

bool er_foeler(uint32_t modul, uint32_t kanal);
bool hent(uint32_t modul, uint32_t kanal, Foeler &ud);
// Tælles op ved hver ny måling eller ændret online-status.
uint32_t version();

}  // namespace temperatur
