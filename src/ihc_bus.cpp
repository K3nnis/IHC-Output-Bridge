#include "ihc_bus.h"

#include <atomic>

#include "board_pins.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

namespace ihc_bus {
namespace {

const char *TAG = "ihc_bus";

constexpr uint32_t kTimerHz = 1000000;  // 1 µs opløsning
constexpr int kBusCore = 1;

// Alt, der bruges fra interruptet, ligger i intern RAM (.bss/.data).
ihc::Sender s_sender;
ihc::Modtager s_modtager;

// Oversætter 16-bit linjeord til GPIO-registermaske (lav og høj byte).
DRAM_ATTR uint32_t s_out_lo[256];
DRAM_ATTR uint32_t s_out_hi[256];
DRAM_ATTR uint32_t s_out_all = 0;

// For hver input-linje: hvilket register (0 = GPIO 0-31, 1 = GPIO 32-48) og bit.
DRAM_ATTR uint8_t s_in_reg[ihc::Modtager::kMaxLines];
DRAM_ATTR uint8_t s_in_bit[ihc::Modtager::kMaxLines];

DRAM_ATTR uint32_t s_sub = 0;
DRAM_ATTR uint32_t s_cpu_mhz = 240;
std::atomic<uint32_t> s_isr_max_cycles{0};

SemaphoreHandle_t s_started = nullptr;

bool IRAM_ATTR on_alarm(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *) {
  const uint32_t t0 = esp_cpu_get_cycle_count();

  if (s_sub == 0) {
    const uint32_t word = s_sender.next_tick();
    const uint32_t set = s_out_lo[word & 0xFF] | s_out_hi[word >> 8];
    REG_WRITE(GPIO_OUT_W1TS_REG, set);
    REG_WRITE(GPIO_OUT_W1TC_REG, s_out_all & ~set);
  }
  s_sub = (s_sub + 1) & 3;

  const uint32_t in[2] = {REG_READ(GPIO_IN_REG), REG_READ(GPIO_IN1_REG)};
  uint32_t levels = 0;
  for (uint32_t i = 0; i < ihc::Modtager::kMaxLines; ++i) {
    levels |= ((in[s_in_reg[i]] >> s_in_bit[i]) & 1u) << i;
  }
  s_modtager.sample(static_cast<uint8_t>(levels));

  const uint32_t dt = esp_cpu_get_cycle_count() - t0;
  if (dt > s_isr_max_cycles.load(std::memory_order_relaxed)) {
    s_isr_max_cycles.store(dt, std::memory_order_relaxed);
  }
  return false;  // ingen task skal vækkes
}

void build_tables() {
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t lo = 0, hi = 0;
    for (uint32_t b = 0; b < 8; ++b) {
      if (i & (1u << b)) {
        lo |= 1u << board::kOutputPins[b];
        hi |= 1u << board::kOutputPins[b + 8];
      }
    }
    s_out_lo[i] = lo;
    s_out_hi[i] = hi;
  }
  s_out_all = s_out_lo[0xFF] | s_out_hi[0xFF];

  for (uint32_t i = 0; i < ihc::Modtager::kMaxLines; ++i) {
    const uint32_t pin = board::kInputPins[i];
    s_in_reg[i] = pin >= 32 ? 1 : 0;
    s_in_bit[i] = pin & 31u;
  }
}

// Kører på kerne 1, så interruptet bliver tildelt den kerne.
void init_task(void *) {
  build_tables();
  s_cpu_mhz = esp_rom_get_cpu_ticks_per_us();
  s_modtager.set_inverted(board::kInvertInputs);

  gptimer_handle_t timer = nullptr;
  gptimer_config_t cfg = {};
  cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  cfg.direction = GPTIMER_COUNT_UP;
  cfg.resolution_hz = kTimerHz;
  cfg.intr_priority = 3;  // højeste prioritet for C-interrupts
  ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &timer));

  gptimer_event_callbacks_t cbs = {};
  cbs.on_alarm = on_alarm;
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &cbs, nullptr));
  ESP_ERROR_CHECK(gptimer_enable(timer));

  gptimer_alarm_config_t alarm = {};
  alarm.alarm_count = ihc::kSampleUs;  // 39 µs, genindlæses af hardwaren
  alarm.reload_count = 0;
  alarm.flags.auto_reload_on_alarm = true;
  ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarm));
  ESP_ERROR_CHECK(gptimer_start(timer));

  ESP_LOGI(TAG, "Bus startet på kerne %d: tick %u µs, sample %u µs", xPortGetCoreID(),
           (unsigned)ihc::kTickUs, (unsigned)ihc::kSampleUs);
  xSemaphoreGive(s_started);
  vTaskDelete(nullptr);
}

}  // namespace

void start() {
  s_started = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(init_task, "ihc_bus_init", 4096, nullptr, configMAX_PRIORITIES - 1, nullptr,
                          kBusCore);
  xSemaphoreTake(s_started, portMAX_DELAY);
  vSemaphoreDelete(s_started);
  s_started = nullptr;
}

void set_output_module(uint32_t module, uint8_t outputs) { s_sender.set_line_data(module, outputs); }

uint8_t output_module(uint32_t module) { return static_cast<uint8_t>(s_sender.line_data(module)); }

const ihc::Sender &sender() { return s_sender; }
ihc::Modtager &modtager() { return s_modtager; }

uint32_t isr_max_us() { return s_isr_max_cycles.load(std::memory_order_relaxed) / s_cpu_mhz; }

}  // namespace ihc_bus
