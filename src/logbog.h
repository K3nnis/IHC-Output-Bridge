// Logbog: de seneste linjer fra systemloggen, så de kan vises i web-UI'et.
//
// Alt, der logges med ESP_LOGx, skrives som før til den serielle port og
// gemmes desuden i en ringbuffer i PSRAM (de seneste 256 linjer).
#pragma once

#include <cstdint>
#include <functional>

namespace logbog {

void init();

// Kalder fn for hver gemt linje med løbenummer > efter, ældste først.
// Returnerer det højeste løbenummer.
uint32_t hent(uint32_t efter, const std::function<void(uint32_t nr, const char *tekst)> &fn);

}  // namespace logbog
