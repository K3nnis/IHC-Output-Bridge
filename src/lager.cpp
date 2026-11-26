#include "lager.h"

#include "esp_log.h"
#include "nvs_flash.h"

namespace lager {
namespace {

const char *TAG = "lager";
constexpr const char *kKonfig = "konfig";

void init_partition(const char *navn) {
  esp_err_t err = nvs_flash_init_partition(navn);
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "Partition \"%s\" formateres (%s)", navn, esp_err_to_name(err));
    ESP_ERROR_CHECK(nvs_flash_erase_partition(navn));
    err = nvs_flash_init_partition(navn);
  }
  ESP_ERROR_CHECK(err);
}

}  // namespace

void init() {
  init_partition(NVS_DEFAULT_PART_NAME);
  init_partition(kKonfig);
}

esp_err_t aaben(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h) {
  return nvs_open_from_partition(kKonfig, ns, mode, h);
}

esp_err_t slet_konfig() {
  nvs_flash_deinit_partition(kKonfig);
  esp_err_t err = nvs_flash_erase_partition(kKonfig);
  if (err == ESP_OK) err = nvs_flash_init_partition(kKonfig);
  return err;
}

}  // namespace lager
