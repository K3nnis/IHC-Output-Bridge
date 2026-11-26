#include "opsaetning.h"

#include <atomic>
#include <cstdio>

#include "esp_log.h"
#include "esp_mac.h"
#include "nvs.h"

namespace opsaetning {
namespace {

const char *TAG = "opsaetning";
constexpr const char *kNamespace = "moduler";

std::atomic<uint16_t> s_output{0xFFFF};
std::atomic<uint8_t> s_input{0xFF};
std::string s_suffix;

}  // namespace

void init() {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READONLY, &h) == ESP_OK) {
    uint16_t o = 0xFFFF;
    uint8_t i = 0xFF;
    if (nvs_get_u16(h, "output", &o) == ESP_OK) s_output = o;
    if (nvs_get_u8(h, "input", &i) == ESP_OK) s_input = i;
    nvs_close(h);
  }

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
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_set_u16(h, "output", output);
  if (err == ESP_OK) err = nvs_set_u8(h, "input", input);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) return false;
  s_output = output;
  s_input = input;
  return true;
}

const std::string &enheds_suffix() { return s_suffix; }

std::string udgangsnavn(uint32_t modul, uint32_t kanal) {
  return "Udgang " + std::to_string(modul) + "." + std::to_string(kanal);
}

std::string indgangsnavn(uint32_t modul, uint32_t kanal) {
  return "Indgang " + std::to_string(modul) + "." + std::to_string(kanal);
}

}  // namespace opsaetning
