// IHC Bridge – hovedprogram.
//
// Fase 1: Projektskelet. Sætter alle IHC-linjer i en sikker starttilstand
// og rapporterer hardware-konfigurationen. Bus-motor, MQTT og web-UI
// tilføjes i de følgende faser.

#include <cinttypes>

#include "board_pins.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"

static const char *TAG = "ihc-bridge";

// Udgangene holdes lave, indtil bus-motoren overtager. Det er den samme
// hviletilstand som mellem to frames, så modulerne ser ingen falske data.
static void init_ihc_pins() {
  uint64_t out_mask = 0;
  for (gpio_num_t pin : board::kOutputPins) out_mask |= 1ULL << pin;
  gpio_config_t out_cfg = {
      .pin_bit_mask = out_mask,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&out_cfg));
  for (gpio_num_t pin : board::kOutputPins) gpio_set_level(pin, 0);

  // 74LVC245 driver input-linjerne aktivt, så der bruges ingen intern pull.
  uint64_t in_mask = 0;
  for (gpio_num_t pin : board::kInputPins) in_mask |= 1ULL << pin;
  gpio_config_t in_cfg = {
      .pin_bit_mask = in_mask,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_ERROR_CHECK(gpio_config(&in_cfg));
}

static void log_hardware() {
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  uint32_t flash_size = 0;
  esp_flash_get_size(nullptr, &flash_size);

  ESP_LOGI(TAG, "Chip: ESP32-S3 rev %d, %d kerner", chip.revision, chip.cores);
  ESP_LOGI(TAG, "Flash: %" PRIu32 " MB", flash_size / (1024 * 1024));
  ESP_LOGI(TAG, "PSRAM: %u MB", (unsigned)(esp_psram_get_size() / (1024 * 1024)));
  ESP_LOGI(TAG, "Outputmoduler: %u, inputmoduler: %u",
           (unsigned)board::kNumOutputModules, (unsigned)board::kNumInputModules);
}

extern "C" void app_main() {
  init_ihc_pins();
  log_hardware();
  ESP_LOGI(TAG, "IHC Bridge startet");
}
