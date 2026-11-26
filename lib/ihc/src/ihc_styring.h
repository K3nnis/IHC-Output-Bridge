// Styring: ejer udgangenes tilstand og forbinder Modtager -> Regeltabel -> Sender.
//
// on_input_change() kaldes direkte fra bus-interruptet, når Modtager har
// bekræftet en ny indgangstilstand. Reglerne udføres med det samme, og de
// ændrede udgange gives til Sender, så de kommer med i næste frame.
//
// Styring låser ikke selv. Kalderen (ihc_bus) sørger for, at interruptet
// og tasks ikke ændrer udgangene samtidig.
#pragma once

#include <atomic>
#include <cstdint>

#include "ihc_protocol.h"
#include "ihc_regler.h"
#include "ihc_sender.h"

namespace ihc {

class Styring {
 public:
  explicit Styring(Sender &sender) : sender_(sender) {}

  // Skifter til en ny regeltabel. Tabellen skal leve, så længe den er i brug.
  void set_regeltabel(const Regeltabel *tabel) { tabel_.store(tabel, std::memory_order_release); }

  // Indgange, der ikke må udløse regler (temperaturfølere). Bit 0 = indgang 1.
  void set_ignorer(uint32_t in_module, uint16_t maske) {
    if (in_module < kInputModules) ignorer_[in_module].store(maske, std::memory_order_relaxed);
  }

  // Fra interruptet. Returnerer bitsæt over ændrede outputmoduler.
  uint16_t on_input_change(uint32_t in_module, uint16_t old_state, uint16_t new_state, bool had_state);

  // Fra tasks (MQTT, web, opstart). Returnerer bitsæt over ændrede outputmoduler.
  uint16_t set_output(uint32_t module, uint32_t channel, bool on);
  uint16_t toggle_output(uint32_t module, uint32_t channel);
  uint16_t set_module(uint32_t module, uint8_t outputs);
  // Indlæser gemt tilstand (ved opstart).
  void load(const uint8_t outputs[kOutputModules]);

  uint8_t output_module(uint32_t module) const { return module < kOutputModules ? outputs_[module] : 0; }
  void copy_outputs(uint8_t out[kOutputModules]) const;

 private:
  void push(uint16_t changed);

  Sender &sender_;
  uint8_t outputs_[kOutputModules] = {};
  std::atomic<const Regeltabel *> tabel_{nullptr};
  std::atomic<uint16_t> ignorer_[kInputModules] = {};
};

}  // namespace ihc
