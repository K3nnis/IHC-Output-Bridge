#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include <vector>

namespace esphome {
namespace ihc_output {

class IHCOutputComponent : public Component {
 public:
  void set_pin(InternalGPIOPin *pin) { pin_ = pin; }
  
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  // Styring af kanaler (1-8)
  void set_channel(int channel, bool state) {
    if (state) {
      this->output_word_ |= (1 << (channel - 1));
    } else {
      this->output_word_ &= ~(1 << (channel - 1));
    }
  }

  // Selve "Tick" logikken flyttet til en statisk metode for timer-kompatibilitet
  void tick();

 protected:
  InternalGPIOPin *pin_;
  uint16_t output_word_{0};    // Ønsket tilstand
  uint32_t output_snapshot_{0}; // Nuærende transmission
  uint32_t output_mask_{0};     
  int16_t pulsepos_{-30};      // Start med delay før første sync
  
  static std::vector<IHCOutputComponent *> instances;
  static bool timer_initialized;
  static void setup_hardware_timer();
};

}  // namespace ihc_output
}  // namespace esphome