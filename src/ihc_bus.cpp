#include "ihc_bus.h"

#include <atomic>

#include "board_pins.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/semphr.h"
#include "ihc_styring.h"
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
ihc::Styring s_styring{s_sender};

// To regeltabeller: én aktiv, én der kan bygges ny uden at forstyrre.
ihc::Regeltabel s_tabeller[2];
uint32_t s_aktiv_tabel = 0;
ihc::Regel s_regler[ihc::Regeltabel::kMaxRegler];
size_t s_antal_regler = 0;
SemaphoreHandle_t s_regler_mutex = nullptr;

// Beskytter udgange og ændringsbits mod samtidig adgang fra interrupt
// (kerne 1) og tasks (kerne 0). Holdes kun i få mikrosekunder.
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
DRAM_ATTR uint16_t s_changed_outputs = 0;
DRAM_ATTR uint8_t s_changed_inputs = 0;
DRAM_ATTR uint32_t s_isr_events = 0;
TaskHandle_t s_event_task = nullptr;

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

// Temperaturkanaler: maske pr. inputmodul og kø fra interrupt til task.
constexpr uint32_t kTempKoe = 256;
DRAM_ATTR uint16_t s_temp_maske[ihc::Modtager::kMaxLines];
DRAM_ATTR uint16_t s_temp_sidst[ihc::Modtager::kMaxLines];
DRAM_ATTR TempHaendelse s_temp_koe[kTempKoe];
std::atomic<uint32_t> s_temp_hoved{0};  // skrives kun af interruptet
std::atomic<uint32_t> s_temp_hale{0};   // skrives kun af temperatur-tasken
std::atomic<uint32_t> s_temp_tabt{0};

// Kaldes af Modtager for hver gyldig frame. Kun ændringer på
// temperaturkanalerne lægges i køen.
void IRAM_ATTR on_frame(void *, uint32_t line, uint16_t data) {
  const uint16_t maske = s_temp_maske[line];
  if (!maske) return;
  const uint16_t v = data & maske;
  if (v == s_temp_sidst[line]) return;
  s_temp_sidst[line] = v;
  const uint32_t hoved = s_temp_hoved.load(std::memory_order_relaxed);
  if (hoved - s_temp_hale.load(std::memory_order_acquire) >= kTempKoe) {
    s_temp_tabt.store(s_temp_tabt.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    return;
  }
  TempHaendelse &h = s_temp_koe[hoved % kTempKoe];
  h.sample = s_modtager.now_samples();
  h.data = v;
  h.modul = static_cast<uint8_t>(line);
  s_temp_hoved.store(hoved + 1, std::memory_order_release);
}

// Kaldes af Modtager inde fra interruptet, når et inputmodul har en ny,
// bekræftet tilstand. Reglerne udføres her og nu.
void IRAM_ATTR on_input_change(void *, uint32_t line, uint16_t old_state, uint16_t new_state, bool had_state) {
  portENTER_CRITICAL_ISR(&s_lock);
  const uint16_t changed = s_styring.on_input_change(line, old_state, new_state, had_state);
  s_changed_outputs |= changed;
  s_changed_inputs |= static_cast<uint8_t>(1u << line);
  portEXIT_CRITICAL_ISR(&s_lock);
  s_isr_events |= kEventInputs | (changed ? kEventOutputs : 0);
}

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

  // Giv hændelses-tasken besked (MQTT, lagring, log). Det sker efter, at
  // udgangen allerede er sat, så det forsinker ikke reaktionen.
  BaseType_t woken = pdFALSE;
  if (s_isr_events && s_event_task) {
    xTaskNotifyFromISR(s_event_task, s_isr_events, eSetBits, &woken);
    s_isr_events = 0;
  }

  const uint32_t dt = esp_cpu_get_cycle_count() - t0;
  if (dt > s_isr_max_cycles.load(std::memory_order_relaxed)) {
    s_isr_max_cycles.store(dt, std::memory_order_relaxed);
  }
  return woken == pdTRUE;
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
  s_modtager.set_change_handler(on_input_change, nullptr);
  s_modtager.set_frame_handler(on_frame, nullptr);

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

void notify(uint32_t bits) {
  if (s_event_task) xTaskNotify(s_event_task, bits, eSetBits);
}

// Udfører en ændring af udgange fra en task under låsen.
template <typename Fn>
void change_outputs(Fn fn) {
  portENTER_CRITICAL(&s_lock);
  const uint16_t changed = fn();
  s_changed_outputs |= changed;
  portEXIT_CRITICAL(&s_lock);
  if (changed) notify(kEventOutputs);
}

}  // namespace

void preload_outputs(const uint8_t outputs[ihc::kOutputModules]) {
  portENTER_CRITICAL(&s_lock);
  s_styring.load(outputs);
  portEXIT_CRITICAL(&s_lock);
}

void set_event_task(TaskHandle_t task) { s_event_task = task; }

void start() {
  s_regler_mutex = xSemaphoreCreateMutex();
  s_started = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(init_task, "ihc_bus_init", 4096, nullptr, configMAX_PRIORITIES - 1, nullptr,
                          kBusCore);
  xSemaphoreTake(s_started, portMAX_DELAY);
  vSemaphoreDelete(s_started);
  s_started = nullptr;
}

size_t set_regler(const ihc::Regel *regler, size_t antal) {
  if (!s_regler_mutex) s_regler_mutex = xSemaphoreCreateMutex();
  xSemaphoreTake(s_regler_mutex, portMAX_DELAY);

  // Byg den inaktive tabel og skift over. Interruptet læser kun den aktive.
  const uint32_t ny = s_aktiv_tabel ^ 1u;
  const size_t brugt = s_tabeller[ny].build(regler, antal);
  s_styring.set_regeltabel(&s_tabeller[ny]);
  s_aktiv_tabel = ny;
  // Et interrupt, der var i gang med den gamle tabel, er færdigt efter få µs.
  // Vent et tick, før den gamle tabel kan genbruges ved næste ændring.
  vTaskDelay(1);

  s_antal_regler = 0;
  for (size_t i = 0; i < antal && s_antal_regler < ihc::Regeltabel::kMaxRegler; ++i) {
    if (ihc::regel_gyldig(regler[i])) s_regler[s_antal_regler++] = regler[i];
  }
  xSemaphoreGive(s_regler_mutex);
  ESP_LOGI(TAG, "%u regler aktive", (unsigned)brugt);
  return brugt;
}

size_t regler(ihc::Regel *ud, size_t max_antal) {
  if (!s_regler_mutex) return 0;
  xSemaphoreTake(s_regler_mutex, portMAX_DELAY);
  const size_t n = s_antal_regler < max_antal ? s_antal_regler : max_antal;
  for (size_t i = 0; i < n; ++i) ud[i] = s_regler[i];
  xSemaphoreGive(s_regler_mutex);
  return n;
}

void set_output(uint32_t module, uint32_t channel, bool on) {
  change_outputs([&] { return s_styring.set_output(module, channel, on); });
}

void toggle_output(uint32_t module, uint32_t channel) {
  change_outputs([&] { return s_styring.toggle_output(module, channel); });
}

void set_output_module(uint32_t module, uint8_t outputs) {
  change_outputs([&] { return s_styring.set_module(module, outputs); });
}

uint8_t output_module(uint32_t module) {
  portENTER_CRITICAL(&s_lock);
  const uint8_t v = s_styring.output_module(module);
  portEXIT_CRITICAL(&s_lock);
  return v;
}

void copy_outputs(uint8_t out[ihc::kOutputModules]) {
  portENTER_CRITICAL(&s_lock);
  s_styring.copy_outputs(out);
  portEXIT_CRITICAL(&s_lock);
}

uint16_t take_changed_outputs() {
  portENTER_CRITICAL(&s_lock);
  const uint16_t v = s_changed_outputs;
  s_changed_outputs = 0;
  portEXIT_CRITICAL(&s_lock);
  return v;
}

uint8_t take_changed_inputs() {
  portENTER_CRITICAL(&s_lock);
  const uint8_t v = s_changed_inputs;
  s_changed_inputs = 0;
  portEXIT_CRITICAL(&s_lock);
  return v;
}

void set_temperatur_kanaler(uint32_t module, uint16_t maske) {
  if (module >= ihc::Modtager::kMaxLines) return;
  s_styring.set_ignorer(module, maske);
  s_temp_sidst[module] = 0xFFFF;  // næste frame giver altid en hændelse med niveauet
  s_temp_maske[module] = maske;
}

bool hent_temp_haendelse(TempHaendelse &ud) {
  const uint32_t hale = s_temp_hale.load(std::memory_order_relaxed);
  if (hale == s_temp_hoved.load(std::memory_order_acquire)) return false;
  ud = s_temp_koe[hale % kTempKoe];
  s_temp_hale.store(hale + 1, std::memory_order_release);
  return true;
}

uint32_t temp_koe_tabt() { return s_temp_tabt.load(std::memory_order_relaxed); }

const ihc::Sender &sender() { return s_sender; }
ihc::Modtager &modtager() { return s_modtager; }

uint32_t isr_max_us() { return s_isr_max_cycles.load(std::memory_order_relaxed) / s_cpu_mhz; }

}  // namespace ihc_bus
