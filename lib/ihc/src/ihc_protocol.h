// IHC-busprotokol: fælles konstanter og hjælpefunktioner.
//
// Samme frame-format bruges af outputmoduler (controlleren sender) og
// inputmoduler (modulet sender, controlleren modtager):
//
//   pause (lav) | startpuls (høj, ~4,3 ms) | 17 bit | pause ...
//
// Hver bit varer 4 ticks à 156 µs:
//   tick 0-1: lav (312 µs)
//   tick 2:   høj
//   tick 3:   lav for "1"/ON  (kort høj puls, 156 µs)
//             høj for "0"/OFF (lang høj puls, 312 µs)
//
// Bit 1-16 er data (kanal 1 sendes først). Bit 17 er paritet:
// 1 hvis antallet af 1-bit i data er lige, ellers 0.
// Outputmoduler bruger kun de første 8 databit.
#pragma once

#include <cstdint>

#if defined(ESP_PLATFORM)
#include "esp_attr.h"
// Kode og data, der bruges fra bus-interruptet, skal ligge i intern RAM,
// så timingen holder, også mens der skrives til flash (NVS, OTA).
#define IHC_IRAM IRAM_ATTR
#else
#define IHC_IRAM
#endif

namespace ihc {

// --- Timing for det, vi sender (målt på en original IHC-controller) ---
constexpr uint32_t kTickUs = 156;
constexpr uint32_t kTicksPerBit = 4;
constexpr uint32_t kPauseTicks = 26;   // 4,06 ms lav
constexpr uint32_t kStartTicks = 28;   // 4,37 ms høj
constexpr uint32_t kFrameBits = 17;    // 16 data + 1 paritet
constexpr uint32_t kDataBits = 16;
constexpr uint32_t kBitTicks = kFrameBits * kTicksPerBit;            // 68
constexpr uint32_t kFrameTicks = kPauseTicks + kStartTicks + kBitTicks;  // 122 = 19 ms

// --- Modtagelse: linjerne samples 4 gange pr. tick ---
constexpr uint32_t kSamplesPerTick = 4;
constexpr uint32_t kSampleUs = kTickUs / kSamplesPerTick;  // 39 µs

// Tilladte pulslængder målt i samples (39 µs). Hver måling kan afvige
// med ±1 sample, og modulerne har deres egen, ikke helt præcise, takt.
constexpr uint16_t kStartMinSamples = 2500 / kSampleUs;   // 64
constexpr uint16_t kStartMaxSamples = 6500 / kSampleUs;   // 166
constexpr uint16_t kOnMinSamples = 2;                     //  78 µs
constexpr uint16_t kOnMaxSamples = 5;                     // 195 µs (nominelt 156)
constexpr uint16_t kOffMinSamples = 7;                    // 273 µs
constexpr uint16_t kOffMaxSamples = 11;                   // 429 µs (nominelt 312)
constexpr uint16_t kBitLowMinSamples = 5;                 // 195 µs
constexpr uint16_t kBitLowMaxSamples = 15;                // 585 µs (nominelt 312/468)

// Et inputmodul regnes for offline, hvis der ikke er modtaget en gyldig
// frame i denne periode (ca. 10 frames).
constexpr uint32_t kOfflineAfterUs = 200000;
constexpr uint32_t kOfflineAfterSamples = kOfflineAfterUs / kSampleUs;

// Tæller 1-bit uden compiler-hjælpefunktioner (de ligger i flash).
IHC_IRAM inline uint32_t count_ones(uint32_t v) {
  uint32_t n = 0;
  while (v) {
    v &= v - 1;
    ++n;
  }
  return n;
}

// Paritetsbit: 1 hvis antallet af 1-bit er lige, 0 hvis ulige.
IHC_IRAM inline uint32_t parity_bit(uint32_t data) {
  return (count_ones(data) & 1u) ? 0u : 1u;
}

}  // namespace ihc
