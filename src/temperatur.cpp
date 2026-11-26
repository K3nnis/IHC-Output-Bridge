#include "temperatur.h"

#include <atomic>
#include <memory>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ihc_bus.h"
#include "ihc_regler.h"
#include "mqtt.h"
#include "opsaetning.h"
#include "web.h"

namespace temperatur {
namespace {

const char *TAG = "temperatur";
constexpr uint32_t kPollMs = 20;

struct Kanal {
  std::unique_ptr<ihc::TemperaturModtager> modtager;
  Foeler foeler;
  int64_t sidst_ms = 0;  // tidspunkt for seneste måling
};

SemaphoreHandle_t s_mutex = nullptr;
Kanal s_kanaler[ihc::kInputModules][ihc::kInputsPerModule];
uint16_t s_maske[ihc::kInputModules] = {};
uint16_t s_niveau[ihc::kInputModules] = {};
std::atomic<uint32_t> s_version{0};
std::atomic<bool> s_genindlaes{true};

// Tidspunkt i ms ud fra bussens sample-tæller (39 µs), uden overløb.
uint64_t s_ms_akku = 0;
uint32_t s_sidste_sample = 0;
bool s_har_sample = false;
uint32_t til_ms(uint32_t sample) {
  if (s_har_sample) s_ms_akku += static_cast<uint64_t>(sample - s_sidste_sample) * ihc::kSampleUs;
  s_sidste_sample = sample;
  s_har_sample = true;
  return static_cast<uint32_t>(s_ms_akku / 1000);
}

int64_t nu_ms() { return esp_timer_get_time() / 1000; }

void genindlaes_masker() {
  for (uint32_t m = 0; m < ihc::kInputModules; ++m) {
    uint16_t maske = 0;
    for (uint32_t k = 0; k < ihc::kInputsPerModule; ++k) {
      if (opsaetning::indgangstype(m + 1, k + 1) == netvaerk::IndgangsType::kTemperatur) maske |= 1u << k;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (uint32_t k = 0; k < ihc::kInputsPerModule; ++k) {
      Kanal &kan = s_kanaler[m][k];
      if (maske & (1u << k)) {
        if (!kan.modtager) kan.modtager.reset(new ihc::TemperaturModtager());
      } else if (kan.modtager) {
        kan.modtager.reset();
        kan.foeler = Foeler();
      }
    }
    s_maske[m] = maske;
    xSemaphoreGive(s_mutex);
    ihc_bus::set_temperatur_kanaler(m, maske);
  }
  s_version.fetch_add(1);
}

void temperatur_task(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(kPollMs));
    if (s_genindlaes.exchange(false)) genindlaes_masker();

    bool aendret = false;
    bool nye_felter = false;
    ihc_bus::TempHaendelse h;
    while (ihc_bus::hent_temp_haendelse(h)) {
      const uint32_t tid = til_ms(h.sample);
      const uint32_t m = h.modul;
      xSemaphoreTake(s_mutex, portMAX_DELAY);
      const uint16_t skift = (h.data ^ s_niveau[m]) & s_maske[m];
      s_niveau[m] = h.data;
      for (uint32_t k = 0; k < ihc::kInputsPerModule; ++k) {
        if (!(skift & (1u << k))) continue;
        Kanal &kan = s_kanaler[m][k];
        if (!kan.modtager) continue;
        kan.modtager->kant(tid, (h.data >> k) & 1u);
        ihc::TemperaturMaaling maal;
        if (kan.modtager->ny_maaling(maal)) {
          Foeler &f = kan.foeler;
          if (!f.online) ESP_LOGI(TAG, "Temperaturføler %u.%u sender", (unsigned)(m + 1), (unsigned)(k + 1));
          if ((maal.har_fugt && !f.set_fugt) || (maal.har_gulv && !f.set_gulv)) nye_felter = true;
          f.har_maaling = true;
          f.maaling = maal;
          f.online = true;
          f.set_fugt |= maal.har_fugt;
          f.set_gulv |= maal.har_gulv;
          kan.sidst_ms = nu_ms();
          aendret = true;
        }
        kan.foeler.gyldige = kan.modtager->gyldige();
        kan.foeler.fejl = kan.modtager->fejl();
      }
      xSemaphoreGive(s_mutex);
    }

    // Følere, der er holdt op med at sende.
    const int64_t nu = nu_ms();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    for (uint32_t m = 0; m < ihc::kInputModules; ++m) {
      for (uint32_t k = 0; k < ihc::kInputsPerModule; ++k) {
        Foeler &f = s_kanaler[m][k].foeler;
        if (f.online && nu - s_kanaler[m][k].sidst_ms > static_cast<int64_t>(kOfflineEfterS) * 1000) {
          f.online = false;
          aendret = true;
          ESP_LOGW(TAG, "Temperaturføler %u.%u har ikke sendt i %u minutter", (unsigned)(m + 1), (unsigned)(k + 1),
                   (unsigned)(kOfflineEfterS / 60));
        }
      }
    }
    xSemaphoreGive(s_mutex);

    if (aendret) {
      s_version.fetch_add(1);
      mqtt::udgange_aendret(0);  // vækker MQTT, som sender nye værdier
      web::aendring();
    }
    if (nye_felter) mqtt::konfiguration_aendret();  // fugt/gulv til Home Assistant
  }
}

}  // namespace

void start() {
  s_mutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(temperatur_task, "temperatur", 4096, nullptr, 4, nullptr, 0);
}

void kanaler_aendret() { s_genindlaes = true; }

bool er_foeler(uint32_t modul, uint32_t kanal) {
  return opsaetning::indgangstype(modul, kanal) == netvaerk::IndgangsType::kTemperatur;
}

bool hent(uint32_t modul, uint32_t kanal, Foeler &ud) {
  if (!s_mutex || modul < 1 || modul > ihc::kInputModules || kanal < 1 || kanal > ihc::kInputsPerModule) return false;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  const Kanal &kan = s_kanaler[modul - 1][kanal - 1];
  const bool findes = static_cast<bool>(kan.modtager);
  ud = kan.foeler;
  if (ud.har_maaling) ud.alder_s = static_cast<uint32_t>((nu_ms() - kan.sidst_ms) / 1000);
  xSemaphoreGive(s_mutex);
  return findes;
}

uint32_t version() { return s_version.load(); }

}  // namespace temperatur
