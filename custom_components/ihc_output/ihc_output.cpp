#include "ihc_output.h"
#include "esphome/core/log.h"
#include <esp_timer.h>

namespace esphome {
namespace ihc_output {

static const char *TAG = "ihc_output";
std::vector<IHCOutputComponent *> IHCOutputComponent::instances;
bool IHCOutputComponent::timer_initialized = false;

void IHCOutputComponent::setup() {
  this->pin_->setup();
  this->pin_->digital_write(false); 
  
  instances.push_back(this);
  
  if (!timer_initialized) {
    this->setup_hardware_timer();
    timer_initialized = true;
  }
}

void IHCOutputComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "IHC Output Module:");
  LOG_PIN("  Pin: ", this->pin_);
}

void IHCOutputComponent::tick() {
  switch (this->state_) {
    
    case STATE_PAUSE:
      this->pin_->digital_write(false); 
      this->tick_counter_++;
      
      if (this->tick_counter_ >= 26) { 
        this->state_ = STATE_START_PULSE;
        this->tick_counter_ = 0;
      }
      break;

    case STATE_START_PULSE:
      this->pin_->digital_write(true); 
      this->tick_counter_++;
      
      if (this->tick_counter_ >= 28) { 
        this->state_ = STATE_SEND_BITS;
        this->tick_counter_ = 0;
        this->bit_index_ = 0;

        this->output_snapshot_ = this->output_word_;
        
        int ones = 0;
        for (int i = 0; i < 16; i++) {
          if (this->output_snapshot_ & (1 << i)) {
            ones++;
          }
        }
        
        // Pariteten er 1, hvis antallet af 1-taller er lige. Den er 0, hvis ulige.
        int parity = (ones % 2 == 0) ? 1 : 0; 
        
        this->output_snapshot_ |= (uint32_t)(parity) << 16;
      }
      break;

    case STATE_SEND_BITS:
      int phase = this->tick_counter_ % 4; 
      bool bit_val = (this->output_snapshot_ & (1 << this->bit_index_)) != 0;

      if (phase == 0 || phase == 1) {
        this->pin_->digital_write(false); 
      } 
      else if (phase == 2) {
        this->pin_->digital_write(true);  
      } 
      else if (phase == 3) {
        this->pin_->digital_write(!bit_val); 
      }

      this->tick_counter_++;
      
      if (phase == 3) {
        this->bit_index_++;
        if (this->bit_index_ >= 17) {
          this->state_ = STATE_PAUSE; 
          this->tick_counter_ = 0;
        }
      }
      break;
  }
}

void IHCOutputComponent::setup_hardware_timer() {
  const esp_timer_create_args_t timer_args = {
      .callback = [](void *arg) {
        for (auto *instance : instances) {
          instance->tick();
        }
      },
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "ihc_periodic_tick",
      .skip_unhandled_events = true
  };
  
  esp_timer_handle_t timer_handle;
  esp_timer_create(&timer_args, &timer_handle);
  
  esp_timer_start_periodic(timer_handle, 156); 
}

}  // namespace ihc_output
}  // namespace esphome
