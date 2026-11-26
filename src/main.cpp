// IHC Bridge – hovedprogram.
//
// Bus, lokale regler, gemt udgangstilstand, Wi-Fi og MQTT kører.
// Web-UI tilføjes i de følgende faser.

#include <cinttypes>

#include "board_pins.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ihc_bus.h"
#include "lager.h"
#include "mqtt.h"
#include "opsaetning.h"
#include "sikkerhed.h"
#include "tilstand.h"
#include "version.h"
#include "web.h"
#include "wifi.h"

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

#ifdef IHC_BUS_TEST
// Testmønster til logic analyzer: hvert outputmodul sender sit eget
// modulnummer binært (modul 1 = 0000 0001, modul 2 = 0000 0010, ... modul 16 = 0001 0000).
// ADVARSEL: tilsluttede relæer skifter efter mønstret.
static void apply_test_pattern() {
  ESP_LOGW(TAG, "BUS-TEST: outputmoduler sender deres modulnummer binært");
  for (uint32_t m = 0; m < board::kNumOutputModules; ++m) {
    ihc_bus::set_output_module(m, static_cast<uint8_t>(m + 1));
  }
}
#endif

// Ny firmware fra web-UI'et skal bekræftes, ellers ruller bootloaderen
// tilbage til den forrige ved næste genstart. Den bekræftes, når den har
// kørt stabilt i 60 sekunder. Crasher den før, rulles der tilbage.
static void bekraeft_firmware_senere() {
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) != ESP_OK ||
      st != ESP_OTA_IMG_PENDING_VERIFY) {
    return;
  }
  ESP_LOGW(TAG, "Ny firmware kører - bekræftes efter 60 s uden fejl");
  esp_timer_create_args_t a = {};
  a.callback = [](void *) {
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "Ny firmware bekræftet");
  };
  a.name = "fw_bekraeft";
  esp_timer_handle_t t;
  esp_timer_create(&a, &t);
  esp_timer_start_once(t, 60ULL * 1000 * 1000);
}

extern "C" void app_main() {
  init_ihc_pins();
  log_hardware();
  lager::init();
  tilstand::init();  // gemte udgange og regler indlæses, før bussen sender
  opsaetning::init();
  sikkerhed::init();
  ihc_bus::start();
  tilstand::start();
  wifi::start();  // kerne 0; bussen kører allerede og påvirkes ikke
  mqtt::start();
  web::start();
  sikkerhed::start_boot_knap();
#ifdef IHC_BUS_TEST
  apply_test_pattern();
#endif
  bekraeft_firmware_senere();
  ESP_LOGI(TAG, "IHC Bridge %s startet", IHC_VERSION);
}
