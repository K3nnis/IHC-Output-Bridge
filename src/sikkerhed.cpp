#include "sikkerhed.h"

#include <cstring>

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lager.h"
#include "mbedtls/pkcs5.h"
#include "nvs.h"
#include "sessioner.h"
#include "web.h"
#include "wifi.h"

namespace sikkerhed {
namespace {

const char *TAG = "sikkerhed";
constexpr const char *kNamespace = "sikkerhed";
constexpr uint32_t kIterationer = 10000;
constexpr size_t kSaltLen = 16;
constexpr size_t kHashLen = 32;
constexpr int64_t kBootHoldMs = 10000;

SemaphoreHandle_t s_mutex = nullptr;
bool s_har_kode = false;
uint8_t s_salt[kSaltLen];
uint8_t s_hash[kHashLen];
uint32_t s_iter = kIterationer;
netvaerk::Sessioner s_sessioner;
netvaerk::LoginBremse s_bremse;

int64_t now_ms() { return esp_timer_get_time() / 1000; }

bool beregn_hash(const std::string &kode, const uint8_t *salt, uint32_t iter, uint8_t *ud) {
  return mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, reinterpret_cast<const unsigned char *>(kode.data()),
                                       kode.size(), salt, kSaltLen, iter, kHashLen, ud) == 0;
}

bool kode_korrekt(const std::string &kode) {
  if (!s_har_kode) return false;
  uint8_t h[kHashLen];
  if (!beregn_hash(kode, s_salt, s_iter, h)) return false;
  return netvaerk::ens_konstant_tid(h, s_hash, kHashLen);
}

bool kode_gyldig(const std::string &kode, std::string &fejl) {
  if (kode.size() < kMinKode) {
    fejl = "Adgangskoden skal være på mindst 8 tegn";
    return false;
  }
  if (kode.size() > kMaxKode) {
    fejl = "Adgangskoden må højst være på 63 tegn";
    return false;
  }
  for (unsigned char c : kode) {
    if (c < 0x20 || c == 0x7F) {
      fejl = "Adgangskoden indeholder ugyldige tegn";
      return false;
    }
  }
  return true;
}

// Gemmer ny kode. Kaldes med s_mutex taget.
bool gem(const std::string &kode, std::string &fejl) {
  uint8_t salt[kSaltLen];
  uint8_t hash[kHashLen];
  esp_fill_random(salt, sizeof(salt));
  if (!beregn_hash(kode, salt, kIterationer, hash)) {
    fejl = "Kunne ikke beregne hash";
    return false;
  }
  nvs_handle_t h;
  esp_err_t err = lager::aaben(kNamespace, NVS_READWRITE, &h);
  if (err == ESP_OK) {
    err = nvs_set_blob(h, "ui_salt", salt, sizeof(salt));
    if (err == ESP_OK) err = nvs_set_blob(h, "ui_hash", hash, sizeof(hash));
    if (err == ESP_OK) err = nvs_set_u32(h, "ui_iter", kIterationer);
    // Nødbetjenings-AP'et (WPA2) skal bruge selve koden.
    if (err == ESP_OK) err = nvs_set_str(h, "noed_kode", kode.c_str());
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
  }
  if (err != ESP_OK) {
    fejl = std::string("Kunne ikke gemme: ") + esp_err_to_name(err);
    return false;
  }
  memcpy(s_salt, salt, sizeof(salt));
  memcpy(s_hash, hash, sizeof(hash));
  s_iter = kIterationer;
  s_har_kode = true;
  return true;
}

void nulstil() {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  nvs_handle_t h;
  if (lager::aaben(kNamespace, NVS_READWRITE, &h) == ESP_OK) {
    nvs_erase_key(h, "ui_salt");
    nvs_erase_key(h, "ui_hash");
    nvs_erase_key(h, "ui_iter");
    nvs_erase_key(h, "noed_kode");
    nvs_commit(h);
    nvs_close(h);
  }
  s_har_kode = false;
  s_sessioner.slet_alle();
  s_bremse.succes();
  xSemaphoreGive(s_mutex);
  web::luk_websockets();
  wifi::noed_kode_aendret();
  ESP_LOGW(TAG, "Adgangskoden er nulstillet med BOOT-knappen. Vælg en ny i web-UI'et.");
}

void boot_task(void *) {
  int64_t trykket_siden = -1;
  bool udfoert = false;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(100));
    const bool trykket = gpio_get_level(board::kBootButtonPin) == 0;
    const int64_t nu = now_ms();
    if (!trykket) {
      trykket_siden = -1;
      udfoert = false;
      continue;
    }
    if (trykket_siden < 0) {
      trykket_siden = nu;
      ESP_LOGI(TAG, "BOOT-knap trykket - hold i 10 s for at nulstille adgangskoden");
    }
    if (!udfoert && nu - trykket_siden >= kBootHoldMs) {
      udfoert = true;
      nulstil();
    }
  }
}

}  // namespace

void init() {
  s_mutex = xSemaphoreCreateMutex();
  nvs_handle_t h;
  if (lager::aaben(kNamespace, NVS_READONLY, &h) != ESP_OK) return;
  size_t salt_len = sizeof(s_salt), hash_len = sizeof(s_hash);
  const bool ok = nvs_get_blob(h, "ui_salt", s_salt, &salt_len) == ESP_OK && salt_len == kSaltLen &&
                  nvs_get_blob(h, "ui_hash", s_hash, &hash_len) == ESP_OK && hash_len == kHashLen &&
                  nvs_get_u32(h, "ui_iter", &s_iter) == ESP_OK;
  nvs_close(h);
  s_har_kode = ok;
  ESP_LOGI(TAG, "%s", ok ? "Adgangskode er sat" : "Ingen adgangskode - første opstart");
}

void start_boot_knap() {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << board::kBootButtonPin;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&cfg);
  xTaskCreatePinnedToCore(boot_task, "boot_knap", 3072, nullptr, 2, nullptr, 0);
}

bool har_kode() { return s_har_kode; }

bool saet_kode(const std::string &ny, std::string &fejl) {
  if (!kode_gyldig(ny, fejl)) return false;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  bool ok = false;
  if (s_har_kode) {
    fejl = "Der er allerede en adgangskode";
  } else {
    ok = gem(ny, fejl);
  }
  xSemaphoreGive(s_mutex);
  if (ok) wifi::noed_kode_aendret();
  return ok;
}

bool skift_kode(const std::string &gammel, const std::string &ny, std::string &fejl) {
  if (!kode_gyldig(ny, fejl)) return false;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  bool ok = false;
  if (!kode_korrekt(gammel)) {
    fejl = "Den nuværende adgangskode er forkert";
  } else {
    ok = gem(ny, fejl);
    if (ok) s_sessioner.slet_alle();  // alle andre logges ud
  }
  xSemaphoreGive(s_mutex);
  if (ok) wifi::noed_kode_aendret();
  return ok;
}

LoginResultat login(const std::string &kode, std::string &token, uint32_t &vent_s) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  const int64_t nu = now_ms();
  LoginResultat r;
  const int64_t vent = s_bremse.ventetid_ms(nu);
  if (vent > 0) {
    vent_s = static_cast<uint32_t>((vent + 999) / 1000);
    r = LoginResultat::kVent;
  } else if (kode_korrekt(kode)) {
    s_bremse.succes();
    uint8_t rnd[32];
    esp_fill_random(rnd, sizeof(rnd));
    token = s_sessioner.opret(nu, rnd);
    r = LoginResultat::kOk;
  } else {
    s_bremse.fejl(nu);
    ESP_LOGW(TAG, "Forkert adgangskode ved login");
    r = LoginResultat::kForkert;
  }
  xSemaphoreGive(s_mutex);
  return r;
}

std::string ny_session() {
  uint8_t rnd[32];
  esp_fill_random(rnd, sizeof(rnd));
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  std::string t = s_sessioner.opret(now_ms(), rnd);
  xSemaphoreGive(s_mutex);
  return t;
}

bool session_gyldig(const std::string &token) {
  if (!s_har_kode) return false;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  const bool ok = s_sessioner.gyldig(token, now_ms());
  xSemaphoreGive(s_mutex);
  return ok;
}

void log_ud(const std::string &token) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_sessioner.slet(token);
  xSemaphoreGive(s_mutex);
}

}  // namespace sikkerhed
