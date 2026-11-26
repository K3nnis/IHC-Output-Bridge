#include "ihc_receiver.h"

namespace ihc {

namespace {
// Tællere skrives kun fra interruptet, så en simpel load+store er nok og
// undgår atomare read-modify-write-instruktioner.
IHC_IRAM inline void bump(std::atomic<uint32_t> &c) {
  c.store(c.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
}
}  // namespace

IHC_IRAM void Receiver::sample(uint8_t levels) {
  now_.store(now_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
  if (inverted_.load(std::memory_order_relaxed)) levels = static_cast<uint8_t>(~levels);

  for (uint32_t i = 0; i < kMaxLines; ++i) {
    Line &l = lines_[i];
    const uint8_t level = (levels >> i) & 1u;
    if (level == l.level) {
      if (l.run < 0xFFFF) ++l.run;
      continue;
    }
    // Niveauskift: den foregående puls er slut.
    if (l.level) {
      end_of_high(l, l.run);
    } else {
      end_of_low(l, l.run);
    }
    l.level = level;
    l.run = 1;
  }
}

IHC_IRAM void Receiver::end_of_high(Line &l, uint16_t run) {
  if (run >= kStartMinSamples && run <= kStartMaxSamples) {
    l.in_frame = true;
    l.bit_index = 0;
    l.shift = 0;
    return;
  }
  if (!l.in_frame) return;  // støj mellem frames ignoreres

  if (run >= kOnMinSamples && run <= kOnMaxSamples) {
    l.shift |= 1u << l.bit_index;
  } else if (!(run >= kOffMinSamples && run <= kOffMaxSamples)) {
    bump(l.framing_errors);
    l.in_frame = false;
    return;
  }

  if (++l.bit_index >= kFrameBits) {
    l.in_frame = false;
    finish_frame(l);
  }
}

IHC_IRAM void Receiver::end_of_low(Line &l, uint16_t run) {
  if (l.in_frame && (run < kBitLowMinSamples || run > kBitLowMaxSamples)) {
    bump(l.framing_errors);
    l.in_frame = false;
  }
}

IHC_IRAM void Receiver::finish_frame(Line &l) {
  const uint32_t data = l.shift & 0xFFFFu;
  const uint32_t rx_parity = (l.shift >> kDataBits) & 1u;
  const bool ok16 = parity_bit(data) == rx_parity;
  const bool ok8 = parity_bit(data & 0xFFu) == rx_parity;
  if (ok16) bump(l.parity16_match);
  if (ok8) bump(l.parity8_match);

  // (if-kæde frem for switch: en switch kan blive til en opslagstabel i flash)
  const auto mode = static_cast<ParityMode>(parity_mode_.load(std::memory_order_relaxed));
  bool accept = ok16 || ok8;
  if (mode == ParityMode::kAll16) accept = ok16;
  else if (mode == ParityMode::kLow8) accept = ok8;
  else if (mode == ParityMode::kIgnore) accept = true;
  if (!accept) {
    bump(l.parity_errors);
    return;
  }
  bump(l.frames_ok);

  const uint32_t now = now_.load(std::memory_order_relaxed);
  const bool fresh =
      !l.have_last || (now - l.last_valid.load(std::memory_order_relaxed)) > kOfflineAfterSamples;
  if (fresh) {
    // Første frame efter opstart eller udfald: vent på bekræftelse.
    l.stable.store(0, std::memory_order_relaxed);
  } else if (data == l.last_data) {
    l.stable.store(data | kStableValid, std::memory_order_relaxed);
  }
  l.last_valid.store(now, std::memory_order_release);
  l.last_data = static_cast<uint16_t>(data);
  l.have_last = true;
}

bool Receiver::online(uint32_t line) const {
  if (line >= kMaxLines) return false;
  const Line &l = lines_[line];
  const uint32_t last = l.last_valid.load(std::memory_order_acquire);
  const uint32_t stable = l.stable.load(std::memory_order_relaxed);
  return (stable & kStableValid) && (now_.load(std::memory_order_relaxed) - last) <= kOfflineAfterSamples;
}

uint16_t Receiver::state(uint32_t line) const {
  if (line >= kMaxLines) return 0;
  return static_cast<uint16_t>(lines_[line].stable.load(std::memory_order_relaxed) & 0xFFFFu);
}

ReceiverStats Receiver::stats(uint32_t line) const {
  ReceiverStats s{};
  if (line >= kMaxLines) return s;
  const Line &l = lines_[line];
  s.frames_ok = l.frames_ok.load(std::memory_order_relaxed);
  s.parity16_match = l.parity16_match.load(std::memory_order_relaxed);
  s.parity8_match = l.parity8_match.load(std::memory_order_relaxed);
  s.parity_errors = l.parity_errors.load(std::memory_order_relaxed);
  s.framing_errors = l.framing_errors.load(std::memory_order_relaxed);
  return s;
}

}  // namespace ihc
