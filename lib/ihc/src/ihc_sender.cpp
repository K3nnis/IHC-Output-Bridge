#include "ihc_sender.h"

namespace ihc {

IHC_IRAM void Sender::set_line_data(uint32_t line, uint16_t data) {
  if (line < kMaxLines) pending_[line].store(data, std::memory_order_relaxed);
}

uint16_t Sender::line_data(uint32_t line) const {
  return line < kMaxLines ? pending_[line].load(std::memory_order_relaxed) : 0;
}

// Tager et øjebliksbillede af alle linjers data lige før databittene, så en
// ændring midt i en frame aldrig giver en blandet frame. At låse så sent som
// muligt (efter pause og startpuls) giver ca. 8 ms kortere reaktionstid.
IHC_IRAM void Sender::prepare_frame() {
  for (uint32_t b = 0; b < kFrameBits; ++b) zero_mask_[b] = 0;
  for (uint32_t line = 0; line < kMaxLines; ++line) {
    const uint32_t data = pending_[line].load(std::memory_order_relaxed);
    const uint32_t frame = data | (parity_bit(data) << kDataBits);
    for (uint32_t b = 0; b < kFrameBits; ++b) {
      if (!(frame & (1u << b))) zero_mask_[b] |= static_cast<uint16_t>(1u << line);
    }
  }
}

IHC_IRAM uint16_t Sender::next_tick() {
  const uint32_t t = tick_;
  tick_ = (t + 1 < kFrameTicks) ? t + 1 : 0;

  if (t == kPauseTicks + kStartTicks) prepare_frame();

  // Pause: alle linjer lave.
  if (t < kPauseTicks) return 0x0000;

  // Startpuls: alle linjer høje.
  if (t < kPauseTicks + kStartTicks) return 0xFFFF;

  // Databit.
  const uint32_t bt = t - kPauseTicks - kStartTicks;
  const uint32_t bit = bt >> 2;    // / kTicksPerBit
  const uint32_t phase = bt & 3u;  // % kTicksPerBit

  if (t + 1 == kFrameTicks) {
    frames_sent_.store(frames_sent_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
  }

  // (if-kæde frem for switch: en switch kan blive til en opslagstabel i flash)
  if (phase < 2) return 0x0000;
  if (phase == 2) return 0xFFFF;
  return zero_mask_[bit];
}

}  // namespace ihc
