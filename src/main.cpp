// IHC Bridge – hovedprogram.
//
// Fase 2: Bus-motoren kører. Input-ændringer og diagnose skrives i loggen.
// Gemt tilstand, regler, MQTT og web-UI tilføjes i de følgende faser.

#include <cinttypes>

#include "board_pins.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ihc_bus.h"

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

// Logger hver ændring på inputmodulerne og en samlet status hvert 10. sekund.
static void monitor_task(void *) {
  auto &rx = ihc_bus::receiver();
  bool was_online[board::kNumInputModules] = {};
  uint16_t last[board::kNumInputModules] = {};
  uint32_t ticks = 0;

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(10));

    for (uint32_t m = 0; m < board::kNumInputModules; ++m) {
      const bool online = rx.online(m);
      if (online != was_online[m]) {
        ESP_LOGI(TAG, "Inputmodul %u er %s", (unsigned)(m + 1), online ? "online" : "OFFLINE");
        was_online[m] = online;
        if (online) last[m] = rx.state(m);  // ingen falske ændringer ved genforbindelse
        continue;
      }
      if (!online) continue;
      const uint16_t now = rx.state(m);
      const uint16_t changed = now ^ last[m];
      for (uint32_t c = 0; c < 16; ++c) {
        if (changed & (1u << c)) {
          ESP_LOGI(TAG, "Input %u.%u: %s", (unsigned)(m + 1), (unsigned)(c + 1), (now & (1u << c)) ? "ON" : "OFF");
        }
      }
      last[m] = now;
    }

    if (++ticks % 1000 == 0) {  // hvert 10. sekund
      ESP_LOGI(TAG, "Status: %" PRIu32 " frames sendt, max ISR-tid %" PRIu32 " µs",
               ihc_bus::sender().frames_sent(), ihc_bus::isr_max_us());
      for (uint32_t m = 0; m < board::kNumInputModules; ++m) {
        const auto s = rx.stats(m);
        if (s.frames_ok == 0 && s.framing_errors == 0 && s.parity_errors == 0) continue;
        ESP_LOGI(TAG,
                 "  Inputmodul %u: %s, tilstand 0x%04X, ok %" PRIu32 ", paritet16 %" PRIu32 ", paritet8 %" PRIu32
                 ", paritetsfejl %" PRIu32 ", pulsfejl %" PRIu32,
                 (unsigned)(m + 1), rx.online(m) ? "online" : "offline", rx.state(m), s.frames_ok,
                 s.parity16_match, s.parity8_match, s.parity_errors, s.framing_errors);
      }
    }
  }
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

extern "C" void app_main() {
  init_ihc_pins();
  log_hardware();
  ihc_bus::start();
#ifdef IHC_BUS_TEST
  apply_test_pattern();
#endif
  xTaskCreatePinnedToCore(monitor_task, "ihc_monitor", 4096, nullptr, 5, nullptr, 0);
  ESP_LOGI(TAG, "IHC Bridge startet");
}
