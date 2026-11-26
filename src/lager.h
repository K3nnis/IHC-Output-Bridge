// Lager: de to NVS-partitioner i flash.
//
//   "nvs"    system (Wi-Fi-kalibrering) og udgangstilstand, der skrives ofte
//   "konfig" indstillinger, adgangskode, navne og regler, der skrives sjældent
//
// Opdelingen gør, at de hyppige skrivninger af udgangstilstand ikke slider
// på eller fylder op i partitionen med indstillinger.
#pragma once

#include "nvs.h"

namespace lager {

// Initialiserer begge partitioner. Formaterer en partition, der ikke kan
// læses (f.eks. efter ny partitionstabel).
void init();

// Åbner et namespace i "konfig"-partitionen.
esp_err_t aaben(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h);

// Sletter alle indstillinger (fabriksnulstilling). Udgangstilstand bevares.
esp_err_t slet_konfig();

}  // namespace lager
