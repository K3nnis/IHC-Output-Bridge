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
  // Vi starter med at skrive HIGH, da IHC data-ledningen hviler på HIGH
  this->pin_->digital_write(true); 
  
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
  // 1. Håndtering af pausen mellem pakker (pulsepos < 0)
  if (++this->pulsepos_ < 0) {
    return;
  }

  // 2. Start på en ny pakke: Tag snapshot og beregn parity præcis som i din originale kode
  if (this->pulsepos_ == 0) {
    this->output_snapshot_ = this->output_word_; // Kopier de 8 ønskede udgange
    this->output_mask_ = 0x01;                   // Start ved bit 0
    
    // DIN ORIGINALE PARITY LOGIK:
    int parity = 1;
    uint8_t temp_mask = 0x01;
    do {
      if ((uint8_t)this->output_snapshot_ & temp_mask)
        parity++;
    } while (temp_mask <<= 1);
    
    // Placer parity-bit på bit 16 (præcis som i din .cpp)
    this->output_snapshot_ |= (uint32_t)(parity & 0x01) << 16;
  }

  // 3. TTL Timing Logik (De 4 ticks pr. bit)
  // Vi genskaber præcis din logik: Tick 0=LOW, Tick 2=HIGH(hvis bit=0), Tick 3=HIGH
  int sub_tick = this->pulsepos_ & 0x3;

  if (sub_tick == 0) {
    // Start på bit: Altid LOW
    this->pin_->digital_write(false); // IHC_LOW
  }
  else if (sub_tick == 2) {
    // Hvis bit er 0, skal den gå HIGH her. Hvis bit er 1, bliver den ved med at være LOW.
    if (!(this->output_snapshot_ & this->output_mask_)) {
      this->pin_->digital_write(true); // IHC_HIGH
    }
  }
  else if (sub_tick == 3) {
    // Ved slutningen af hver bit går vi altid HIGH
    this->pin_->digital_write(true); // IHC_HIGH
    this->output_mask_ <<= 1;        // Gør klar til næste bit
  }

  // 4. Afslutning af pakken efter 17 bits (16 data + 1 parity)
  if (this->pulsepos_ >= (4 * 17)) {
    this->pulsepos_ = -30; // 30 ticks pause (ca. 30-45ms pause)
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
      .name = "ihc_periodic_tick"
  };
  
  esp_timer_handle_t timer_handle;
  esp_timer_create(&timer_args, &timer_handle);
  
  // Vi sætter den til 1000 mikrosekunder (1ms). 
  // Dette svarer til timingen i de fleste IHC-projekter.
  esp_timer_start_periodic(timer_handle, 1000); 
}

}  // namespace ihc_output
}  // namespace esphome