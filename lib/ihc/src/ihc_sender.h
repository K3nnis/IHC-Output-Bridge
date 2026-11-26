// Sender: genererer IHC-frames til op til 16 linjer på én gang.
//
// Alle linjer deler samme takt, men hver linje har sine egne data.
// next_tick() kaldes hvert 156 µs og returnerer et 16-bit ord, hvor
// bit n er niveauet på linje n i netop det tick.
#pragma once

#include <atomic>
#include <cstdint>

#include "ihc_protocol.h"

namespace ihc {

class Sender {
 public:
  static constexpr uint32_t kMaxLines = 16;

  // Sætter de data, linjen skal sende fra næste frame. Kan kaldes fra
  // en almindelig task, mens interruptet kører.
  void set_line_data(uint32_t line, uint16_t data);
  uint16_t line_data(uint32_t line) const;

  // Kaldes én gang pr. tick (156 µs) fra bus-interruptet.
  uint16_t next_tick();

  // Antal færdigsendte frames (til diagnose).
  uint32_t frames_sent() const { return frames_sent_.load(std::memory_order_relaxed); }

 private:
  void prepare_frame();

  std::atomic<uint16_t> pending_[kMaxLines] = {};

  // Kun brugt fra interruptet.
  uint32_t tick_ = 0;
  // zero_mask_[b]: linjer hvor bit b er 0 (OFF) og derfor holdes høj i tick 3.
  uint16_t zero_mask_[kFrameBits] = {};
  std::atomic<uint32_t> frames_sent_{0};
};

}  // namespace ihc
