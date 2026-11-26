// Pin-plan for YD-ESP32-S3 (HW678) / ESP32-S3-DevKitC-1 med N16R8.
//
// Alle IHC-pins samlet ét sted. Ret kun her, hvis fortrådningen ændres.
//
// Undgåede pins:
//   GPIO 0, 3, 45, 46   strapping-pins (GPIO 0 = BOOT-knap)
//   GPIO 19, 20         native USB (D-/D+)
//   GPIO 26-32          intern flash (ikke ført ud på WROOM-1)
//   GPIO 33-37          oktal PSRAM
//   GPIO 43, 44         UART0 til CH343P (COM-porten)
#pragma once

#include <cstddef>
#include "driver/gpio.h"

namespace board {

// IHC outputmoduler 1-16 via SN74HCT541N (3,3 V -> 5 V TTL).
// Alle ligger under GPIO 32, så de kan sættes med én registerskrivning.
constexpr gpio_num_t kOutputPins[] = {
    GPIO_NUM_1,  GPIO_NUM_2,  GPIO_NUM_4,  GPIO_NUM_5,   // modul 1-4
    GPIO_NUM_6,  GPIO_NUM_7,  GPIO_NUM_8,  GPIO_NUM_9,   // modul 5-8
    GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13,  // modul 9-12
    GPIO_NUM_14, GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17,  // modul 13-16
};

// IHC inputmoduler 1-8 via SN74LVC245N (5 V TTL -> 3,3 V, DIR fast B->A).
constexpr gpio_num_t kInputPins[] = {
    GPIO_NUM_18, GPIO_NUM_21, GPIO_NUM_38, GPIO_NUM_39,  // modul 1-4
    GPIO_NUM_40, GPIO_NUM_41, GPIO_NUM_42, GPIO_NUM_47,  // modul 5-8
};

constexpr size_t kNumOutputModules = sizeof(kOutputPins) / sizeof(kOutputPins[0]);
constexpr size_t kNumInputModules = sizeof(kInputPins) / sizeof(kInputPins[0]);

// Sæt til true, hvis input-linjerne vendes (f.eks. optokobler i stedet for 74LVC245).
constexpr bool kInvertInputs = false;

// Output-pins skal ligge under GPIO 32, så alle sættes med én registerskrivning.
constexpr bool outputs_in_low_bank() {
  for (gpio_num_t pin : kOutputPins)
    if (pin >= 32) return false;
  return true;
}
static_assert(outputs_in_low_bank(), "Alle output-pins skal være GPIO 0-31");

// WS2812 status-LED. Kun forbundet, hvis loddebroen "RGB" er lukket.
constexpr gpio_num_t kStatusLedPin = GPIO_NUM_48;

// BOOT-knappen. Hold 10 s under drift for at nulstille UI-adgangskoden.
constexpr gpio_num_t kBootButtonPin = GPIO_NUM_0;

static_assert(kNumOutputModules == 16, "Der skal være 16 output-pins");
static_assert(kNumInputModules == 8, "Der skal være 8 input-pins");

}  // namespace board
