// Tilstand: gemmer og genskaber udgange og regler, og behandler hændelser
// fra bussen (log nu, MQTT og web i senere faser).
//
// Udgangene gemmes i flash (NVS) 2 sekunder efter sidste ændring, dog
// senest 10 sekunder efter første ændring, så hyppige tryk ikke slider
// på flashen. Gemningen sker i en task på kerne 0 og forsinker aldrig
// bussen eller reglerne.
#pragma once

#include <cstddef>

#include "ihc_regler.h"

namespace tilstand {

// Initialiserer NVS og indlæser gemte udgange og regler i bussen.
// Kaldes før ihc_bus::start().
void init();

// Starter hændelses-tasken. Kaldes efter ihc_bus::start().
void start();

// Aktiverer nye regler med det samme og gemmer dem i flash.
bool gem_regler(const ihc::Regel *regler, size_t antal);

}  // namespace tilstand
