#include "tilstand.h"

#include <cinttypes>
#include <cstring>

#include "board_pins.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ihc_bus.h"
#include "mqtt.h"
#include "opsaetning.h"
#include "web.h"
#include "nvs.h"
#include "lager.h"

namespace tilstand {
namespace {

const char *TAG = "tilstand";

constexpr const char *kNamespace = "ihc";  // udgange: "nvs", regler: "konfig"
constexpr const char *kKeyUdgange = "udgange";
constexpr const char *kKeyRegler = "regler";

constexpr int64_t kGemEfterStilleMs = 2000;  // gem 2 s efter sidste ændring
constexpr int64_t kGemSenestMs = 10000;      // ... men senest 10 s efter første
constexpr uint32_t kPollMs = 50;
constexpr int64_t kStatusHvertMs = 60000;

constexpr size_t kReglerBufSize =
    ihc::kReglerHeaderBytes + ihc::Regeltabel::kMaxRegler * ihc::kReglerBytesPerRegel;

// Store buffere ligger statisk frem for på task-stakken. Mutex'en beskytter
// dem, når regler gemmes fra flere tasks (f.eks. web og MQTT).
ihc::Regel s_regel_buf[ihc::Regeltabel::kMaxRegler];
uint8_t s_byte_buf[kReglerBufSize];
SemaphoreHandle_t s_buf_mutex = nullptr;

int64_t now_ms() { return esp_timer_get_time() / 1000; }

// konfig=true: "konfig"-partitionen (regler), ellers "nvs" (udgange).
esp_err_t aaben(bool konfig, nvs_open_mode_t mode, nvs_handle_t *h) {
  return konfig ? lager::aaben(kNamespace, mode, h) : nvs_open(kNamespace, mode, h);
}

bool nvs_write_blob(const char *key, const void *data, size_t len, bool konfig = false) {
  nvs_handle_t h;
  esp_err_t err = aaben(konfig, NVS_READWRITE, &h);
  if (err == ESP_OK) {
    err = nvs_set_blob(h, key, data, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
  }
  if (err != ESP_OK) ESP_LOGE(TAG, "Kunne ikke gemme '%s': %s", key, esp_err_to_name(err));
  return err == ESP_OK;
}

bool nvs_read_blob(const char *key, void *data, size_t *len, bool konfig = false) {
  nvs_handle_t h;
  if (aaben(konfig, NVS_READONLY, &h) != ESP_OK) return false;
  const esp_err_t err = nvs_get_blob(h, key, data, len);
  nvs_close(h);
  return err == ESP_OK;
}

void gem_udgange() {
#ifdef IHC_BUS_TEST
  // Testfirmwarens mønster må aldrig gemmes: så ville den almindelige
  // firmware genskabe det og tænde relæerne efter næste opstart.
  return;
#endif
  uint8_t udgange[ihc::kOutputModules];
  ihc_bus::copy_outputs(udgange);
  if (nvs_write_blob(kKeyUdgange, udgange, sizeof(udgange))) ESP_LOGD(TAG, "Udgange gemt");
}

void log_udgange(uint16_t changed, uint8_t sidst[ihc::kOutputModules]) {
  for (uint32_t m = 0; m < ihc::kOutputModules; ++m) {
    if (!(changed & (1u << m))) continue;
    const uint8_t nu = ihc_bus::output_module(m);
    const uint8_t diff = nu ^ sidst[m];
    for (uint32_t c = 0; c < ihc::kOutputsPerModule; ++c) {
      if (diff & (1u << c)) {
        ESP_LOGI(TAG, "Udgang %u.%u: %s", (unsigned)(m + 1), (unsigned)(c + 1), (nu & (1u << c)) ? "ON" : "OFF");
      }
    }
    sidst[m] = nu;
  }
}

void log_status() {
  auto &rx = ihc_bus::modtager();
  ESP_LOGI(TAG, "Status: %" PRIu32 " frames sendt, max ISR-tid %" PRIu32 " µs", ihc_bus::sender().frames_sent(),
           ihc_bus::isr_max_us());
  for (uint32_t m = 0; m < board::kNumInputModules; ++m) {
    const auto s = rx.stats(m);
    if (s.frames_ok == 0 && s.framing_errors == 0 && s.parity_errors == 0) continue;
    ESP_LOGI(TAG,
             "  Inputmodul %u: %s, tilstand 0x%04X, ok %" PRIu32 ", paritet16 %" PRIu32 ", paritet8 %" PRIu32
             ", paritetsfejl %" PRIu32 ", pulsfejl %" PRIu32,
             (unsigned)(m + 1), rx.online(m) ? "online" : "offline", rx.state(m), s.frames_ok, s.parity16_match,
             s.parity8_match, s.parity_errors, s.framing_errors);
  }
}

void event_task(void *) {
  auto &rx = ihc_bus::modtager();
  bool online[board::kNumInputModules] = {};
  uint16_t sidste_input[board::kNumInputModules] = {};
  uint8_t sidste_udgange[ihc::kOutputModules];
  ihc_bus::copy_outputs(sidste_udgange);

  int64_t foerste_aendring = 0;
  int64_t sidste_aendring = 0;
  bool skal_gemmes = false;
  int64_t naeste_status = now_ms() + kStatusHvertMs;

  for (;;) {
    uint32_t bits = 0;
    xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(kPollMs));
    const int64_t nu = now_ms();

    // Inputmoduler, der går online/offline. Ved genforbindelse vises
    // tilstanden uden at blive tolket som tryk.
    for (uint32_t m = 0; m < board::kNumInputModules; ++m) {
      const bool er_online = rx.online(m);
      if (er_online == online[m]) continue;
      online[m] = er_online;
      if (er_online) {
        sidste_input[m] = rx.state(m);
        opsaetning::marker_inputmodul_set(m + 1);  // nye moduler slås til automatisk
      }
      ESP_LOGI(TAG, "Inputmodul %u er %s", (unsigned)(m + 1), er_online ? "online" : "OFFLINE");
    }

    if (bits & ihc_bus::kEventInputs) {
      const uint8_t moduler = ihc_bus::take_changed_inputs();
      for (uint32_t m = 0; m < board::kNumInputModules; ++m) {
        if (!(moduler & (1u << m)) || !online[m]) continue;
        const uint16_t state = rx.state(m);
        const uint16_t diff = state ^ sidste_input[m];
        for (uint32_t c = 0; c < ihc::kInputsPerModule; ++c) {
          if (diff & (1u << c)) {
            ESP_LOGI(TAG, "Input %u.%u: %s", (unsigned)(m + 1), (unsigned)(c + 1), (state & (1u << c)) ? "ON" : "OFF");
          }
        }
        sidste_input[m] = state;
      }
      mqtt::indgange_aendret(moduler);
      web::aendring();
    }

    if (bits & ihc_bus::kEventOutputs) {
      const uint16_t changed = ihc_bus::take_changed_outputs();
      if (changed) {
        mqtt::udgange_aendret(changed);  // først: MQTT og web skal ikke vente på log
        web::aendring();
        log_udgange(changed, sidste_udgange);
        if (!skal_gemmes) foerste_aendring = nu;
        sidste_aendring = nu;
        skal_gemmes = true;
      }
    }

    if (skal_gemmes && (nu - sidste_aendring >= kGemEfterStilleMs || nu - foerste_aendring >= kGemSenestMs)) {
      gem_udgange();
      skal_gemmes = false;
    }

    if (nu >= naeste_status) {
      log_status();
      naeste_status = nu + kStatusHvertMs;
    }
  }
}

}  // namespace

void init() {
  s_buf_mutex = xSemaphoreCreateMutex();
  uint8_t udgange[ihc::kOutputModules] = {};
  size_t len = sizeof(udgange);
  if (nvs_read_blob(kKeyUdgange, udgange, &len) && len == sizeof(udgange)) {
    ESP_LOGI(TAG, "Gemt udgangstilstand genskabt");
  } else {
    memset(udgange, 0, sizeof(udgange));
    ESP_LOGI(TAG, "Ingen gemt udgangstilstand - alle udgange slukket");
  }
  ihc_bus::preload_outputs(udgange);

  len = sizeof(s_byte_buf);
  int antal = 0;
  if (nvs_read_blob(kKeyRegler, s_byte_buf, &len, true)) {
    antal = ihc::regler_fra_bytes(s_byte_buf, len, s_regel_buf, ihc::Regeltabel::kMaxRegler);
    if (antal < 0) {
      ESP_LOGE(TAG, "Gemte regler er ugyldige og ignoreres");
      antal = 0;
    }
  }
  ihc_bus::set_regler(s_regel_buf, static_cast<size_t>(antal));
}

void start() {
  TaskHandle_t task = nullptr;
  xTaskCreatePinnedToCore(event_task, "tilstand", 4096, nullptr, 10, &task, 0);
  ihc_bus::set_event_task(task);
}

bool gem_regler(const ihc::Regel *regler, size_t antal) {
  if (antal > ihc::Regeltabel::kMaxRegler) return false;
  for (size_t i = 0; i < antal; ++i) {
    if (!ihc::regel_gyldig(regler[i])) return false;
  }
  ihc_bus::set_regler(regler, antal);
  xSemaphoreTake(s_buf_mutex, portMAX_DELAY);
  const size_t len = ihc::regler_til_bytes(regler, antal, s_byte_buf, sizeof(s_byte_buf));
  const bool ok = len > 0 && nvs_write_blob(kKeyRegler, s_byte_buf, len, true);
  xSemaphoreGive(s_buf_mutex);
  return ok;
}

}  // namespace tilstand
