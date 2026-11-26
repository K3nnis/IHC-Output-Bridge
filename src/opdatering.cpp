#include "opdatering.h"

#include <cstring>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lager.h"
#include "nvs.h"
#include "tilstand.h"
#include "version.h"
#include "versioner.h"
#include "wifi.h"

namespace opdatering {
namespace {

const char *TAG = "opdatering";
constexpr const char *kNamespace = "opdatering";
constexpr size_t kMaxSvar = 256 * 1024;  // GitHubs svar med ændringsnoter
constexpr int kTimeoutMs = 20000;

enum class Job : uint8_t { kSoeg, kInstaller };

SemaphoreHandle_t s_mutex = nullptr;
Status s_status;
bool s_travl = false;
int64_t s_soegt_us = 0;

void saet(Tilstand t, const std::string &fejl = "") {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_status.tilstand = t;
  s_status.fejl = fejl;
  xSemaphoreGive(s_mutex);
  if (!fejl.empty()) ESP_LOGW(TAG, "%s", fejl.c_str());
}

bool paa_nettet(std::string &fejl) {
  if (!wifi::status().forbundet) {
    fejl = "IHC Bridge er ikke forbundet til dit Wi-Fi";
    return false;
  }
  return true;
}

// Henter releases fra GitHubs API. Svaret lægges i PSRAM.
bool hent_releases(std::string &fejl, Release &ud, bool &fundet) {
  const std::string url = std::string("https://api.github.com/repos/") + kRepo + "/releases?per_page=10";
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = kTimeoutMs;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 1024;
  cfg.user_agent = "IHC-Bridge/" IHC_VERSION;
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (!c) {
    fejl = "Kunne ikke starte forbindelsen";
    return false;
  }
  esp_http_client_set_header(c, "Accept", "application/vnd.github+json");

  char *buf = nullptr;
  size_t laest = 0;
  bool ok = false;
  if (esp_http_client_open(c, 0) != ESP_OK) {
    fejl = "Kan ikke nå GitHub. Er der adgang til internettet?";
  } else {
    esp_http_client_fetch_headers(c);
    const int kode = esp_http_client_get_status_code(c);
    if (kode == 403 || kode == 429) {
      fejl = "GitHub afviste forespørgslen midlertidigt. Prøv igen om en time.";
    } else if (kode != 200) {
      fejl = "GitHub svarede med fejl " + std::to_string(kode);
    } else {
      buf = static_cast<char *>(heap_caps_malloc(kMaxSvar, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
      if (!buf) {
        fejl = "Ikke nok hukommelse";
      } else {
        for (;;) {
          const int r = esp_http_client_read(c, buf + laest, kMaxSvar - laest);
          if (r <= 0) break;
          laest += r;
          if (laest >= kMaxSvar) break;
        }
        ok = laest > 0 && laest < kMaxSvar;
        if (!ok) fejl = laest ? "Svaret fra GitHub var for stort" : "Intet svar fra GitHub";
      }
    }
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);

  if (ok) {
    bool test;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    test = s_status.testversioner;
    xSemaphoreGive(s_mutex);
    fundet = vaelg_release(buf, laest, kRepo, test, ud, fejl);
    ok = fundet || fejl.empty();
  }
  heap_caps_free(buf);
  return ok;
}

void soeg_job() {
  std::string fejl;
  if (!paa_nettet(fejl)) return saet(Tilstand::kFejl, fejl);
  Release r;
  bool fundet = false;
  if (!hent_releases(fejl, r, fundet)) return saet(Tilstand::kFejl, fejl);

  const bool nyere = fundet && netvaerk::er_nyere(r.version, IHC_VERSION);
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_status.har_release = fundet;
  if (fundet) s_status.release = r;
  s_status.tilstand = nyere ? Tilstand::kFundet : Tilstand::kNyeste;
  s_status.fejl.clear();
  s_soegt_us = esp_timer_get_time();
  xSemaphoreGive(s_mutex);
  ESP_LOGI(TAG, "Nyeste version på GitHub: %s (installeret: %s)", fundet ? r.version.c_str() : "ingen", IHC_VERSION);
}

void installer_job() {
  std::string fejl;
  if (!paa_nettet(fejl)) return saet(Tilstand::kFejl, fejl);
  Release r;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  r = s_status.release;
  s_status.procent = 0;
  xSemaphoreGive(s_mutex);
  saet(Tilstand::kHenter);
  ESP_LOGW(TAG, "Henter version %s fra GitHub", r.version.c_str());

  esp_http_client_config_t http = {};
  http.url = r.fil.c_str();
  http.crt_bundle_attach = esp_crt_bundle_attach;
  http.timeout_ms = kTimeoutMs;
  // GitHub viderestiller til en lang adresse; plads til lange headere.
  http.buffer_size = 4096;
  http.buffer_size_tx = 2048;
  http.keep_alive_enable = true;
  http.user_agent = "IHC-Bridge/" IHC_VERSION;
  esp_https_ota_config_t cfg = {};
  cfg.http_config = &http;

  esp_https_ota_handle_t h = nullptr;
  if (esp_https_ota_begin(&cfg, &h) != ESP_OK) return saet(Tilstand::kFejl, "Kunne ikke hente filen fra GitHub");

  // Tjek at filen er firmware til IHC Bridge, før der skrives mere.
  esp_app_desc_t ny;
  if (esp_https_ota_get_img_desc(h, &ny) != ESP_OK ||
      strncmp(ny.project_name, esp_app_get_description()->project_name, sizeof(ny.project_name)) != 0) {
    esp_https_ota_abort(h);
    return saet(Tilstand::kFejl, "Filen er ikke firmware til IHC Bridge");
  }

  esp_err_t err;
  while ((err = esp_https_ota_perform(h)) == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
    const int total = esp_https_ota_get_image_size(h);
    const int laest = esp_https_ota_get_image_len_read(h);
    if (total > 0) {
      xSemaphoreTake(s_mutex, portMAX_DELAY);
      s_status.procent = static_cast<int>(static_cast<int64_t>(laest) * 100 / total);
      xSemaphoreGive(s_mutex);
    }
  }
  if (err != ESP_OK || !esp_https_ota_is_complete_data_received(h)) {
    esp_https_ota_abort(h);
    return saet(Tilstand::kFejl, "Overførslen blev afbrudt. Prøv igen.");
  }
  err = esp_https_ota_finish(h);
  if (err == ESP_ERR_OTA_VALIDATE_FAILED) return saet(Tilstand::kFejl, "Firmwaren er beskadiget (kontrolsum passer ikke)");
  if (err != ESP_OK) return saet(Tilstand::kFejl, "Kunne ikke aktivere den nye firmware");

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_status.procent = 100;
  xSemaphoreGive(s_mutex);
  saet(Tilstand::kGenstarter);
  ESP_LOGW(TAG, "Version %s er installeret - genstarter", r.version.c_str());
  tilstand::gem_udgange_nu();
  vTaskDelay(pdMS_TO_TICKS(2000));  // så web-UI'et når at se, at den genstarter
  esp_restart();
}

void task(void *arg) {
  const Job job = static_cast<Job>(reinterpret_cast<uintptr_t>(arg));
  if (job == Job::kSoeg) {
    soeg_job();
  } else {
    installer_job();
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_travl = false;
  xSemaphoreGive(s_mutex);
  vTaskDelete(nullptr);
}

bool start_job(Job job, Tilstand t) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  const bool travl = s_travl;
  if (!travl) {
    s_travl = true;
    s_status.tilstand = t;
    s_status.fejl.clear();
  }
  xSemaphoreGive(s_mutex);
  if (travl) return false;
  // HTTPS kræver en større stak; den frigives, når jobbet er færdigt.
  if (xTaskCreatePinnedToCore(task, "opdatering", 8192, reinterpret_cast<void *>(static_cast<uintptr_t>(job)), 3,
                              nullptr, 0) != pdPASS) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_travl = false;
    xSemaphoreGive(s_mutex);
    saet(Tilstand::kFejl, "Ikke nok hukommelse til at starte");
    return false;
  }
  return true;
}

}  // namespace

void init() {
  s_mutex = xSemaphoreCreateMutex();
  nvs_handle_t h;
  if (lager::aaben(kNamespace, NVS_READONLY, &h) == ESP_OK) {
    uint8_t v = 0;
    if (nvs_get_u8(h, "test", &v) == ESP_OK) s_status.testversioner = v != 0;
    nvs_close(h);
  }
}

Status status() {
  Status st;
  if (!s_mutex) return st;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  st = s_status;
  st.sidst_soegt_s = s_soegt_us ? (esp_timer_get_time() - s_soegt_us) / 1000000 : -1;
  xSemaphoreGive(s_mutex);
  return st;
}

bool soeg() { return start_job(Job::kSoeg, Tilstand::kSoeger); }

bool installer(const std::string &version, std::string &fejl) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  const bool klar = s_status.har_release && s_status.tilstand == Tilstand::kFundet && s_status.release.version == version;
  xSemaphoreGive(s_mutex);
  if (!klar) {
    fejl = "Søg efter opdatering først";
    return false;
  }
  if (!start_job(Job::kInstaller, Tilstand::kHenter)) {
    fejl = "En søgning eller opdatering er allerede i gang";
    return false;
  }
  return true;
}

bool saet_testversioner(bool ja) {
  nvs_handle_t h;
  if (lager::aaben(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_set_u8(h, "test", ja ? 1 : 0);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) return false;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_status.testversioner = ja;
  // Et tidligere søgeresultat gælder ikke længere.
  if (s_status.tilstand == Tilstand::kFundet || s_status.tilstand == Tilstand::kNyeste) {
    s_status.tilstand = Tilstand::kKlar;
    s_status.har_release = false;
  }
  xSemaphoreGive(s_mutex);
  return true;
}

}  // namespace opdatering
