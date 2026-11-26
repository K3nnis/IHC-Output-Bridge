// Fælles hjælpere til IHC-tests: syntetiske bølgeformer og afkodning.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "ihc_modtager.h"
#include "ihc_protocol.h"
#include "ihc_sender.h"

namespace ihc_test {
using namespace ihc;

// ---------------------------------------------------------------------------
// Hjælpere
// ---------------------------------------------------------------------------

// Bølgeform for én linje som en række (varighed i ns, niveau).
struct Wave {
  struct Seg {
    uint64_t end_ns;
    uint8_t level;
  };
  std::vector<Seg> segs;
  uint64_t total_ns = 0;

  void add(double us, uint8_t level) {
    total_ns += static_cast<uint64_t>(us * 1000.0);
    segs.push_back({total_ns, level});
  }

  // Én IHC-frame som et inputmodul ville sende den. scale > 1 = langsommere modul.
  void add_frame_parity(uint16_t data, uint32_t parity, double scale = 1.0) {
    add(4000 * scale, 0);  // pause
    add(4370 * scale, 1);  // startpuls
    const uint32_t frame = data | (parity << 16);
    for (uint32_t b = 0; b < 17; ++b) {
      const bool on = frame & (1u << b);
      add(312 * scale, 0);
      if (on) {
        add(156 * scale, 1);
        add(156 * scale, 0);
      } else {
        add(312 * scale, 1);
      }
    }
  }
  void add_frame(uint16_t data, double scale = 1.0) { add_frame_parity(data, parity_bit(data), scale); }

  uint8_t level_at(uint64_t t_ns) const {
    auto it = std::upper_bound(segs.begin(), segs.end(), t_ns,
                               [](uint64_t t, const Seg &s) { return t < s.end_ns; });
    return it == segs.end() ? 0 : it->level;  // efter sidste segment: linjen er lav
  }
};

// Sampler bølgeformerne hvert 39 µs (startfase offset_ns) og fodrer Modtager.
inline void feed(Modtager &rx, const std::vector<Wave> &waves, uint64_t offset_ns = 0) {
  uint64_t total = 0;
  for (const auto &w : waves) total = w.total_ns > total ? w.total_ns : total;
  for (uint64_t t = offset_ns; t < total; t += kSampleUs * 1000) {
    uint8_t levels = 0;
    for (size_t i = 0; i < waves.size(); ++i) levels |= waves[i].level_at(t) << i;
    rx.sample(levels);
  }
}

inline void feed_idle(Modtager &rx, uint32_t us, uint8_t levels = 0) {
  for (uint32_t i = 0; i < us / kSampleUs; ++i) rx.sample(levels);
}

// Kører Sender i et antal ticks og sender linje 0-7 videre til Modtager.
inline void loopback(Sender &tx, Modtager &rx, uint32_t ticks) {
  for (uint32_t t = 0; t < ticks; ++t) {
    const uint8_t levels = static_cast<uint8_t>(tx.next_tick() & 0xFF);
    for (uint32_t s = 0; s < kSamplesPerTick; ++s) rx.sample(levels);
  }
}

// Decoder én linje direkte fra Senders tick-ord (uafhængig af Modtager).
inline uint32_t decode_ticks(const std::vector<uint16_t> &ticks, uint32_t line) {
  uint32_t frame = 0;
  for (uint32_t b = 0; b < kFrameBits; ++b) {
    const uint32_t base = kPauseTicks + kStartTicks + b * kTicksPerBit;
    const bool short_high = !(ticks[base + 3] & (1u << line));
    if (short_high) frame |= 1u << b;
  }
  return frame;
}

inline std::vector<uint16_t> one_frame(Sender &tx) {
  std::vector<uint16_t> v;
  for (uint32_t t = 0; t < kFrameTicks; ++t) v.push_back(tx.next_tick());
  return v;
}

}  // namespace ihc_test
