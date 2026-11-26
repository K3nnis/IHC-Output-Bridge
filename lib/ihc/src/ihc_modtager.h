// Modtager: modtager IHC-frames fra op til 8 inputmoduler på én gang.
//
// sample() kaldes hvert 39 µs med niveauet på alle linjer (bit n = linje n).
// Hver linje har sin egen tilstandsmaskine, der måler pulslængder og
// samler frames. En ny tilstand accepteres først, når to frames i træk
// er ens. Det beskytter mod enkeltfejl på ledningen.
#pragma once

#include <atomic>
#include <cstdint>

#include "ihc_protocol.h"

namespace ihc {

// Hvilken paritetsregel inputmodulerne bruger, er endnu ikke målt.
// kAuto accepterer frames, der passer med én af reglerne, og statistikken
// viser, hvilken regel modulerne faktisk følger.
enum class ParityMode : uint8_t {
  kAuto = 0,   // accepter hvis paritet over alle 16 bit ELLER bit 1-8 passer
  kAll16 = 1,  // paritet over alle 16 databit
  kLow8 = 2,   // paritet over bit 1-8
  kIgnore = 3, // ingen paritetskontrol
};

struct ModtagerStats {
  uint32_t frames_ok;        // accepterede frames
  uint32_t parity16_match;   // frames hvor paritet over 16 bit passer
  uint32_t parity8_match;    // frames hvor paritet over bit 1-8 passer
  uint32_t parity_errors;    // afviste frames pga. paritet
  uint32_t framing_errors;   // pulser med ugyldig længde
};

class Modtager {
 public:
  static constexpr uint32_t kMaxLines = 8;

  // Kaldes fra interruptet, når en linjes bekræftede tilstand ændrer sig.
  // had_state er false ved første bekræftelse efter opstart eller udfald;
  // så kendes den gamle tilstand ikke, og der må ikke reageres på flanker.
  // Funktionen skal ligge i IRAM og være hurtig.
  using ChangeHandler = void (*)(void *ctx, uint32_t line, uint16_t old_state, uint16_t new_state,
                                 bool had_state);
  void set_change_handler(ChangeHandler fn, void *ctx) {
    handler_ctx_ = ctx;
    handler_ = fn;
  }

  void set_parity_mode(ParityMode mode) { parity_mode_.store(static_cast<uint8_t>(mode)); }
  ParityMode parity_mode() const { return static_cast<ParityMode>(parity_mode_.load()); }

  // Til opkobling med optokobler, som vender signalet.
  void set_inverted(bool inverted) { inverted_.store(inverted); }

  // Kaldes hvert 39 µs fra bus-interruptet.
  void sample(uint8_t levels);

  // --- Læses fra almindelige tasks ---

  // Sand når linjen har leveret gyldige frames inden for de sidste 200 ms
  // og der findes en bekræftet tilstand.
  bool online(uint32_t line) const;
  // Bekræftet tilstand af de 16 indgange (bit 0 = indgang 1).
  uint16_t state(uint32_t line) const;
  ModtagerStats stats(uint32_t line) const;
  // Antal samples siden start (39 µs pr. stk).
  uint32_t now_samples() const { return now_.load(std::memory_order_relaxed); }

 private:
  static constexpr uint32_t kStableValid = 1u << 31;

  struct Line {
    // Kun brugt fra interruptet.
    uint8_t level = 0;
    uint16_t run = 0;
    bool in_frame = false;
    uint8_t bit_index = 0;
    uint32_t shift = 0;
    bool have_last = false;
    uint16_t last_data = 0;

    // Delt med tasks.
    std::atomic<uint32_t> stable{0};
    std::atomic<uint32_t> last_valid{0};
    std::atomic<uint32_t> frames_ok{0};
    std::atomic<uint32_t> parity16_match{0};
    std::atomic<uint32_t> parity8_match{0};
    std::atomic<uint32_t> parity_errors{0};
    std::atomic<uint32_t> framing_errors{0};
  };

  void end_of_high(Line &l, uint16_t run);
  void end_of_low(Line &l, uint16_t run);
  void finish_frame(Line &l);

  Line lines_[kMaxLines];
  ChangeHandler handler_ = nullptr;
  void *handler_ctx_ = nullptr;
  std::atomic<uint32_t> now_{0};
  std::atomic<uint8_t> parity_mode_{static_cast<uint8_t>(ParityMode::kAuto)};
  std::atomic<bool> inverted_{false};
};

}  // namespace ihc
