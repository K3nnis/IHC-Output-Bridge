#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include <vector>

namespace esphome {
namespace ihc_output {

enum IHCState {
  STATE_PAUSE,
  STATE_START_PULSE,
  STATE_SEND_BITS
};

class IHCOutputComponent : public Component {
 public:
  void set_pin(InternalGPIOPin *pin) { pin_ = pin; }
  
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  void set_channel(int channel, bool state) {
    if (state) {
      this->output_word_ |= (1 << (channel - 1));
    } else {
      this->output_word_ &= ~(1 << (channel - 1));
    }
  }

  void tick();

 protected:
  InternalGPIOPin *pin_;
  uint16_t output_word_{0};    
  uint32_t output_snapshot_{0}; 
  
  IHCState state_{STATE_PAUSE};
  int tick_counter_{0};         
  int bit_index_{0};            
  
  static std::vector<IHCOutputComponent *> instances;
  static bool timer_initialized;
  static void setup_hardware_timer();
};

}  // namespace ihc_output
}  // namespace esphome