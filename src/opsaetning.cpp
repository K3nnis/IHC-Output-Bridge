#include "opsaetning.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ihc_regler.h"
#include "lager.h"
#include "mqtt.h"
#include "temperatur.h"
#include "nvs.h"

namespace opsaetning {
namespace {

const char *TAG = "opsaetning";
constexpr const char *kNsModuler = "moduler";
constexpr const char *kNsNavne = "navne";
constexpr const char *kNsTyper = "typer";

constexpr uint32_t kUdgange = ihc::kOutputModules * ihc::kOutputsPerModule;  // 128
constexpr uint32_t kIndgange = ihc::kInputModules * ihc::kInputsPerModule;   // 128

std::atomic<uint16_t> s_output{0};
std::atomic<uint8_t> s_input{0};
std::string s_suffix;

// Navne og typer holdes i RAM og beskyttes af s_mutex.
SemaphoreHandle_t s_mutex = nullptr;
char s_udgangsnavn[kUdgange][kMaxNavn + 1];
char s_indgangsnavn[kIndgange][kMaxNavn + 1];
uint8_t s_udgangstype[kUdgange];
uint8_t s_indgangstype[kIndgange];

bool gyldig_udgang(uint32_t m, uint32_t k) {
  return m >= 1 && m <= ihc::kOutputModules && k >= 1 && k <= ihc::kOutputsPerModule;
}
bool gyldig_indgang(uint32_t m, uint32_t k) {
  return m >= 1 && m <= ihc::kInputModules && k >= 1 && k <= ihc::kInputsPerModule;
}
uint32_t udgang_idx(uint32_t m, uint32_t k) { return (m - 1) * ihc::kOutputsPerModule + (k - 1); }
uint32_t indgang_idx(uint32_t m, uint32_t k) { return (m - 1) * ihc::kInputsPerModule + (k - 1); }

std::string noegle(char prefix, uint32_t m, uint32_t k) {
  char buf[12];
  snprintf(buf, sizeof(buf), "%c%u_%u", prefix, (unsigned)m, (unsigned)k);
  return buf;
}

bool gem_moduler(uint16_t output, uint8_t input) {
  nvs_handle_t h;
  if (lager::aaben(kNsModuler, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_set_u16(h, "output", output);
  if (err == ESP_OK) err = nvs_set_u8(h, "input", input);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

void laes_navne() {
  nvs_handle_t h;
  if (lager::aaben(kNsNavne, NVS_READONLY, &h) != ESP_OK) return;
  for (uint32_t m = 1; m <= ihc::kOutputModules; ++m) {
    for (uint32_t k = 1; k <= ihc::kOutputsPerModule; ++k) {
      size_t len = kMaxNavn + 1;
      char *buf = s_udgangsnavn[udgang_idx(m, k)];
      if (nvs_get_str(h, noegle('u', m, k).c_str(), buf, &len) != ESP_OK) buf[0] = '\0';
    }
  }
  for (uint32_t m = 1; m <= ihc::kInputModules; ++m) {
    for (uint32_t k = 1; k <= ihc::kInputsPerModule; ++k) {
      size_t len = kMaxNavn + 1;
      char *buf = s_indgangsnavn[indgang_idx(m, k)];
      if (nvs_get_str(h, noegle('i', m, k).c_str(), buf, &len) != ESP_OK) buf[0] = '\0';
    }
  }
  nvs_close(h);
}

void laes_typer() {
  nvs_handle_t h;
  if (lager::aaben(kNsTyper, NVS_READONLY, &h) != ESP_OK) return;
  size_t len = sizeof(s_udgangstype);
  if (nvs_get_blob(h, "udgange", s_udgangstype, &len) != ESP_OK || len != sizeof(s_udgangstype)) {
    memset(s_udgangstype, 0, sizeof(s_udgangstype));
  }
  len = sizeof(s_indgangstype);
  if (nvs_get_blob(h, "indgange", s_indgangstype, &len) != ESP_OK || len != sizeof(s_indgangstype)) {
    memset(s_indgangstype, 0, sizeof(s_indgangstype));
  }
  nvs_close(h);
  for (auto &t : s_udgangstype)
    if (t >= netvaerk::kAntalUdgangsTyper) t = 0;
  for (auto &t : s_indgangstype)
    if (t >= netvaerk::kAntalIndgangsTyper) t = 0;
}

// Gemmer navn og typetabel for én kanal. Kaldes med s_mutex taget.
bool gem_kanal(char prefix, uint32_t m, uint32_t k, const char *navn, const char *type_key, const uint8_t *typer,
               size_t typer_len) {
  nvs_handle_t h;
  esp_err_t err = lager::aaben(kNsNavne, NVS_READWRITE, &h);
  if (err != ESP_OK) return false;
  const std::string key = noegle(prefix, m, k);
  if (navn[0]) {
    err = nvs_set_str(h, key.c_str(), navn);
  } else {
    err = nvs_erase_key(h, key.c_str());
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
  }
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) return false;

  if (lager::aaben(kNsTyper, NVS_READWRITE, &h) != ESP_OK) return false;
  err = nvs_set_blob(h, type_key, typer, typer_len);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

}  // namespace

void init() {
  s_mutex = xSemaphoreCreateMutex();
  nvs_handle_t h;
  if (lager::aaben(kNsModuler, NVS_READONLY, &h) == ESP_OK) {
    uint16_t o = 0;
    uint8_t i = 0;
    if (nvs_get_u16(h, "output", &o) == ESP_OK) s_output = o;
    if (nvs_get_u8(h, "input", &i) == ESP_OK) s_input = i;
    nvs_close(h);
  }
  laes_navne();
  laes_typer();

  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buf[8];
  snprintf(buf, sizeof(buf), "%02x%02x%02x", mac[3], mac[4], mac[5]);
  s_suffix = buf;
  ESP_LOGI(TAG, "Enheds-ID %s, aktive moduler: output 0x%04X, input 0x%02X", s_suffix.c_str(),
           (unsigned)s_output.load(), (unsigned)s_input.load());
}

uint16_t aktive_outputmoduler() { return s_output.load(); }
uint8_t aktive_inputmoduler() { return s_input.load(); }

bool gem_aktive_moduler(uint16_t output, uint8_t input) {
  if (!gem_moduler(output, input)) return false;
  s_output = output;
  s_input = input;
  mqtt::konfiguration_aendret();
  return true;
}

bool marker_inputmodul_set(uint32_t modul) {
  if (modul < 1 || modul > ihc::kInputModules) return false;
  const uint8_t bit = static_cast<uint8_t>(1u << (modul - 1));
  const uint8_t foer = s_input.load();
  if (foer & bit) return false;
  if (!gem_moduler(s_output.load(), static_cast<uint8_t>(foer | bit))) return false;
  s_input = static_cast<uint8_t>(foer | bit);
  ESP_LOGI(TAG, "Inputmodul %u fundet og slået til", (unsigned)modul);
  mqtt::konfiguration_aendret();
  return true;
}

const std::string &enheds_suffix() { return s_suffix; }

std::string udgangsnavn(uint32_t modul, uint32_t kanal) {
  if (gyldig_udgang(modul, kanal) && s_mutex) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    std::string navn = s_udgangsnavn[udgang_idx(modul, kanal)];
    xSemaphoreGive(s_mutex);
    if (!navn.empty()) return navn;
  }
  return "Udgang " + std::to_string(modul) + "." + std::to_string(kanal);
}

std::string indgangsnavn(uint32_t modul, uint32_t kanal) {
  if (gyldig_indgang(modul, kanal) && s_mutex) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    std::string navn = s_indgangsnavn[indgang_idx(modul, kanal)];
    xSemaphoreGive(s_mutex);
    if (!navn.empty()) return navn;
  }
  return "Indgang " + std::to_string(modul) + "." + std::to_string(kanal);
}

bool har_eget_udgangsnavn(uint32_t modul, uint32_t kanal) {
  return gyldig_udgang(modul, kanal) && s_udgangsnavn[udgang_idx(modul, kanal)][0] != '\0';
}

bool har_eget_indgangsnavn(uint32_t modul, uint32_t kanal) {
  return gyldig_indgang(modul, kanal) && s_indgangsnavn[indgang_idx(modul, kanal)][0] != '\0';
}

netvaerk::UdgangsType udgangstype(uint32_t modul, uint32_t kanal) {
  if (!gyldig_udgang(modul, kanal)) return netvaerk::UdgangsType::kLys;
  return static_cast<netvaerk::UdgangsType>(s_udgangstype[udgang_idx(modul, kanal)]);
}

netvaerk::IndgangsType indgangstype(uint32_t modul, uint32_t kanal) {
  if (!gyldig_indgang(modul, kanal)) return netvaerk::IndgangsType::kTryk;
  return static_cast<netvaerk::IndgangsType>(s_indgangstype[indgang_idx(modul, kanal)]);
}

bool gem_udgang(uint32_t modul, uint32_t kanal, const std::string &navn, netvaerk::UdgangsType type) {
  if (!gyldig_udgang(modul, kanal) || navn.size() > kMaxNavn) return false;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  const uint32_t i = udgang_idx(modul, kanal);
  strncpy(s_udgangsnavn[i], navn.c_str(), kMaxNavn);
  s_udgangsnavn[i][kMaxNavn] = '\0';
  s_udgangstype[i] = static_cast<uint8_t>(type);
  const bool ok = gem_kanal('u', modul, kanal, s_udgangsnavn[i], "udgange", s_udgangstype, sizeof(s_udgangstype));
  xSemaphoreGive(s_mutex);
  if (ok) mqtt::konfiguration_aendret();
  return ok;
}

bool gem_indgang(uint32_t modul, uint32_t kanal, const std::string &navn, netvaerk::IndgangsType type) {
  if (!gyldig_indgang(modul, kanal) || navn.size() > kMaxNavn) return false;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  const uint32_t i = indgang_idx(modul, kanal);
  strncpy(s_indgangsnavn[i], navn.c_str(), kMaxNavn);
  s_indgangsnavn[i][kMaxNavn] = '\0';
  s_indgangstype[i] = static_cast<uint8_t>(type);
  const bool ok = gem_kanal('i', modul, kanal, s_indgangsnavn[i], "indgange", s_indgangstype, sizeof(s_indgangstype));
  xSemaphoreGive(s_mutex);
  if (ok) {
    temperatur::kanaler_aendret();
    mqtt::konfiguration_aendret();
  }
  return ok;
}

}  // namespace opsaetning
