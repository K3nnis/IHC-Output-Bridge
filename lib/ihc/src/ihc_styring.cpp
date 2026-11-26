#include "ihc_styring.h"

namespace ihc {

IHC_IRAM void Styring::push(uint16_t changed) {
  for (uint32_t m = 0; m < kOutputModules; ++m) {
    if (changed & (1u << m)) sender_.set_line_data(m, outputs_[m]);
  }
}

IHC_IRAM uint16_t Styring::on_input_change(uint32_t in_module, uint16_t old_state, uint16_t new_state,
                                           bool had_state) {
  // Uden kendt tidligere tilstand (opstart, genforbindelse) kan vi ikke
  // vide, om der er trykket, så der reageres ikke.
  if (!had_state) return 0;
  const Regeltabel *tabel = tabel_.load(std::memory_order_acquire);
  if (!tabel) return 0;
  const uint16_t ignorer = in_module < kInputModules ? ignorer_[in_module].load(std::memory_order_relaxed) : 0;
  const uint16_t rising = static_cast<uint16_t>(new_state & ~old_state & ~ignorer);
  if (!rising) return 0;
  const uint16_t changed = tabel->apply(in_module, rising, outputs_);
  push(changed);
  return changed;
}

uint16_t Styring::set_output(uint32_t module, uint32_t channel, bool on) {
  if (module >= kOutputModules || channel >= kOutputsPerModule) return 0;
  const uint8_t bit = static_cast<uint8_t>(1u << channel);
  const uint8_t next = on ? (outputs_[module] | bit) : (outputs_[module] & ~bit);
  return set_module(module, next);
}

uint16_t Styring::toggle_output(uint32_t module, uint32_t channel) {
  if (module >= kOutputModules || channel >= kOutputsPerModule) return 0;
  return set_module(module, static_cast<uint8_t>(outputs_[module] ^ (1u << channel)));
}

uint16_t Styring::set_module(uint32_t module, uint8_t outputs) {
  if (module >= kOutputModules || outputs_[module] == outputs) return 0;
  outputs_[module] = outputs;
  const uint16_t changed = static_cast<uint16_t>(1u << module);
  push(changed);
  return changed;
}

void Styring::load(const uint8_t outputs[kOutputModules]) {
  for (uint32_t m = 0; m < kOutputModules; ++m) outputs_[m] = outputs[m];
  push(0xFFFF);
}

void Styring::copy_outputs(uint8_t out[kOutputModules]) const {
  for (uint32_t m = 0; m < kOutputModules; ++m) out[m] = outputs_[m];
}

}  // namespace ihc
