#include "wifi.h"

#include <cstring>
#include <string>
#include <vector>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lager.h"
#include "nvs.h"

namespace wifi {
namespace {

const char *TAG = "wifi";

constexpr const char *kNvsWifi = "wifi";
constexpr const char *kNvsSikkerhed = "sikkerhed";
constexpr const char *kKeyNoedKode = "noed_kode";

constexpr uint32_t kBitForbundet = 1u << 0;
constexpr uint32_t kBitAfbrudt = 1u << 1;
constexpr uint32_t kBitNyKonfig = 1u << 2;
constexpr uint32_t kBitNyKode = 1u << 3;
constexpr uint32_t kPollMs = 500;
constexpr uint32_t kNyKonfigVentMs = 1500;

TaskHandle_t s_task = nullptr;
esp_netif_t *s_sta = nullptr;
esp_netif_t *s_ap = nullptr;

SemaphoreHandle_t s_mutex = nullptr;  // beskytter s_status og s_konfig
Status s_status;

netvaerk::WifiStyring s_styring;
Konfig s_konfig;
char s_noed_kode[65] = {};
netvaerk::ApTilstand s_aktiv_ap = netvaerk::ApTilstand::kSlukket;
bool s_ap_sat = false;

int64_t now_ms() { return esp_timer_get_time() / 1000; }

bool har_netvaerk() { return s_konfig.ssid[0] != '\0'; }
// WPA2 kræver mindst 8 tegn.
bool har_noed_kode() { return strlen(s_noed_kode) >= 8; }

void laes_str(nvs_handle_t h, const char *key, char *buf, size_t size) {
  size_t len = size;
  if (nvs_get_str(h, key, buf, &len) != ESP_OK) buf[0] = '\0';
}

void laes_konfig() {
  Konfig k;
  nvs_handle_t h;
  if (lager::aaben(kNvsWifi, NVS_READONLY, &h) == ESP_OK) {
    laes_str(h, "ssid", k.ssid, sizeof(k.ssid));
    laes_str(h, "kode", k.kode, sizeof(k.kode));
    laes_str(h, "vaert", k.vaertsnavn, sizeof(k.vaertsnavn));
    uint8_t fast = 0;
    nvs_get_u8(h, "fast_ip", &fast);
    k.fast_ip = fast != 0;
    nvs_get_u32(h, "ip", &k.ip);
    nvs_get_u32(h, "maske", &k.maske);
    nvs_get_u32(h, "gw", &k.gateway);
    nvs_get_u32(h, "dns", &k.dns);
    nvs_close(h);
  }
  if (k.vaertsnavn[0] == '\0') strncpy(k.vaertsnavn, kStandardVaertsnavn, sizeof(k.vaertsnavn) - 1);
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_konfig = k;
  xSemaphoreGive(s_mutex);
}

void laes_noed_kode() {
  s_noed_kode[0] = '\0';
  nvs_handle_t h;
  if (lager::aaben(kNvsSikkerhed, NVS_READONLY, &h) == ESP_OK) {
    laes_str(h, kKeyNoedKode, s_noed_kode, sizeof(s_noed_kode));
    nvs_close(h);
  }
}

// mDNS gør enheden synlig som <værtsnavn>.local på både dit Wi-Fi og
// opsætnings-/nødbetjenings-Wi-Fi'et, uden at routeren skal hjælpe.
void anvend_mdns() {
  mdns_hostname_set(s_konfig.vaertsnavn);
  ESP_LOGI(TAG, "Kan nås på http://%s.local", s_konfig.vaertsnavn);
}

void start_mdns() {
  if (mdns_init() != ESP_OK) {
    ESP_LOGE(TAG, "mDNS kunne ikke starte");
    return;
  }
  mdns_instance_name_set("IHC Bridge");
  mdns_service_add("IHC Bridge", "_http", "_tcp", 80, nullptr, 0);
  anvend_mdns();
}

void anvend_sta_konfig() {
  wifi_config_t cfg = {};
  strncpy(reinterpret_cast<char *>(cfg.sta.ssid), s_konfig.ssid, sizeof(cfg.sta.ssid));
  strncpy(reinterpret_cast<char *>(cfg.sta.password), s_konfig.kode, sizeof(cfg.sta.password));
  cfg.sta.threshold.authmode = s_konfig.kode[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
  cfg.sta.pmf_cfg.capable = true;
  // Ved flere access points med samme navn (mesh) vælges det stærkeste.
  cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
  if (err != ESP_OK) ESP_LOGE(TAG, "STA-konfiguration fejlede: %s", esp_err_to_name(err));

  esp_netif_set_hostname(s_sta, s_konfig.vaertsnavn);

  if (s_konfig.fast_ip) {
    esp_netif_dhcpc_stop(s_sta);
    esp_netif_ip_info_t ip = {};
    ip.ip.addr = s_konfig.ip;
    ip.netmask.addr = s_konfig.maske;
    ip.gw.addr = s_konfig.gateway;
    esp_netif_set_ip_info(s_sta, &ip);
    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = s_konfig.dns ? s_konfig.dns : s_konfig.gateway;
    esp_netif_set_dns_info(s_sta, ESP_NETIF_DNS_MAIN, &dns);
  } else {
    esp_netif_dhcpc_start(s_sta);  // fejler harmløst, hvis den allerede kører
  }
}

// Sætter Wi-Fi-tilstand og AP efter, hvad WifiStyring har besluttet.
void anvend_ap(netvaerk::ApTilstand ap) {
  if (s_ap_sat && ap == s_aktiv_ap) return;
  s_ap_sat = true;
  s_aktiv_ap = ap;

  if (ap == netvaerk::ApTilstand::kSlukket) {
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "Access point slukket");
    return;
  }

  // AP+STA også uden gemt netværk: STA-delen skal være slået til for at
  // kunne scanne efter netværk fra opsætningssiden.
  esp_wifi_set_mode(WIFI_MODE_APSTA);
  wifi_config_t cfg = {};
  const bool noed = ap == netvaerk::ApTilstand::kNoedbetjening;
  const char *ssid = noed ? kNoedbetjeningSsid : kOpsaetningSsid;
  strncpy(reinterpret_cast<char *>(cfg.ap.ssid), ssid, sizeof(cfg.ap.ssid));
  cfg.ap.ssid_len = strlen(ssid);
  cfg.ap.channel = 1;  // følger routerens kanal, når enheden også er forbundet
  cfg.ap.max_connection = 4;
  if (noed) {
    strncpy(reinterpret_cast<char *>(cfg.ap.password), s_noed_kode, sizeof(cfg.ap.password));
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.pmf_cfg.capable = true;
  } else {
    cfg.ap.authmode = WIFI_AUTH_OPEN;
  }
  const esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
  if (err != ESP_OK) ESP_LOGE(TAG, "AP-konfiguration fejlede: %s", esp_err_to_name(err));
  ESP_LOGW(TAG, "Access point \"%s\" er åbnet (%s) - adresse 192.168.4.1", ssid,
           noed ? "beskyttet med UI-adgangskoden" : "åbent");
}

void opdater_status() {
  Status st;
  const int64_t nu = now_ms();
  st.forbundet = s_styring.fase() == netvaerk::Fase::kForbundet;
  strncpy(st.ssid, s_konfig.ssid, sizeof(st.ssid) - 1);
  strncpy(st.vaertsnavn, s_konfig.vaertsnavn, sizeof(st.vaertsnavn) - 1);
  st.ap = s_aktiv_ap;
  st.forsoeg = s_styring.forsoeg_siden_tab();
  st.ms_til_naeste = s_styring.ms_til_naeste(nu);
  if (st.forbundet) {
    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(s_sta, &ip) == ESP_OK) {
      snprintf(st.ip, sizeof(st.ip), IPSTR, IP2STR(&ip.ip));
    }
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) st.rssi = info.rssi;
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_status = st;
  xSemaphoreGive(s_mutex);
}

void on_event(void *, esp_event_base_t base, int32_t id, void *data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    const auto *e = static_cast<wifi_event_sta_disconnected_t *>(data);
    ESP_LOGW(TAG, "Ingen forbindelse til routeren (årsag %d)", e->reason);
    xTaskNotify(s_task, kBitAfbrudt, eSetBits);
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto *e = static_cast<ip_event_got_ip_t *>(data);
    ESP_LOGI(TAG, "Forbundet med IP " IPSTR, IP2STR(&e->ip_info.ip));
    xTaskNotify(s_task, kBitForbundet, eSetBits);
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
    ESP_LOGI(TAG, "En enhed har forbundet sig til access point'et");
  }
}

void wifi_task(void *) {
  s_styring.start(now_ms(), har_netvaerk(), har_noed_kode());
  // Tilstanden (STA/AP/AP+STA) sættes først; STA-konfiguration kræver STA.
  anvend_ap(s_styring.ap(now_ms()));
  if (har_netvaerk()) {
    anvend_sta_konfig();
    ESP_LOGI(TAG, "Forbinder til \"%s\" som \"%s\"", s_konfig.ssid, s_konfig.vaertsnavn);
  } else {
    ESP_LOGW(TAG, "Intet Wi-Fi-netværk gemt");
  }
  ESP_ERROR_CHECK(esp_wifi_start());
  // Strømsparing slås fra: giver hurtigere svar på MQTT og web.
  esp_wifi_set_ps(WIFI_PS_NONE);

  for (;;) {
    uint32_t bits = 0;
    xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(kPollMs));
    const int64_t nu = now_ms();

    if (bits & kBitNyKode) {
      laes_noed_kode();
      s_styring.noed_kode_sat(har_noed_kode());
      if (s_aktiv_ap == netvaerk::ApTilstand::kNoedbetjening) s_ap_sat = false;  // ny kode på AP'et
    }
    if (bits & kBitNyKonfig) {
      // Vent lidt, så svaret på "Gem" når frem til browseren, før
      // forbindelsen ændres. Ellers mister telefonen forbindelsen til
      // opsætnings-Wi-Fi'et, før den har fået svar.
      vTaskDelay(pdMS_TO_TICKS(kNyKonfigVentMs));
      esp_wifi_disconnect();  // fejler harmløst, hvis der ikke er forbindelse
      laes_konfig();
      s_styring.netvaerk_gemt(nu);
      // AP'et kører allerede i AP+STA og genstartes ikke (det ville smide
      // tilsluttede telefoner af).
      anvend_ap(s_styring.ap(nu));
      anvend_sta_konfig();
      anvend_mdns();  // værtsnavnet kan være ændret
      ESP_LOGI(TAG, "Nyt netværk gemt: \"%s\"", s_konfig.ssid);
    }
    if (bits & kBitForbundet) s_styring.forbundet(nu);
    if (bits & kBitAfbrudt) s_styring.afbrudt(nu);

    if (s_styring.tick(nu)) {
      ESP_LOGI(TAG, "Forbindelsesforsøg %u til \"%s\"", (unsigned)s_styring.forsoeg_siden_tab(), s_konfig.ssid);
      const esp_err_t err = esp_wifi_connect();
      if (err != ESP_OK) {
        ESP_LOGW(TAG, "Forsøg kunne ikke startes: %s", esp_err_to_name(err));
        s_styring.afbrudt(nu);
      }
    }

    anvend_ap(s_styring.ap(nu));
    opdater_status();
  }
}

}  // namespace

void start() {
  s_mutex = xSemaphoreCreateMutex();
  laes_konfig();
  laes_noed_kode();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  s_sta = esp_netif_create_default_wifi_sta();
  s_ap = esp_netif_create_default_wifi_ap();

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  // Konfigurationen styres af os (NVS-namespace "wifi"), ikke af driveren.
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  esp_wifi_set_country_code("DK", true);

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr));

  start_mdns();
  xTaskCreatePinnedToCore(wifi_task, "wifi", 4096, nullptr, 5, &s_task, 0);
}

bool vaertsnavn_gyldigt(const char *navn) {
  // Små bogstaver a-z, tal og bindestreg; ikke bindestreg først eller sidst.
  const size_t n = strlen(navn);
  if (n == 0 || n > 32 || navn[0] == '-' || navn[n - 1] == '-') return false;
  for (size_t i = 0; i < n; ++i) {
    const char c = navn[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return true;
}

Status status() {
  Status st;
  if (!s_mutex) return st;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  st = s_status;
  xSemaphoreGive(s_mutex);
  return st;
}

bool hent_konfig(Konfig &k, bool skjul_kode) {
  if (!s_mutex) return false;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  k = s_konfig;
  xSemaphoreGive(s_mutex);
  if (skjul_kode) memset(k.kode, 0, sizeof(k.kode));
  return k.ssid[0] != '\0';
}

bool gem_konfig(const Konfig &k) {
  if (k.ssid[0] == '\0') return false;
  nvs_handle_t h;
  esp_err_t err = lager::aaben(kNvsWifi, NVS_READWRITE, &h);
  if (err != ESP_OK) return false;
  err = nvs_set_str(h, "ssid", k.ssid);
  if (err == ESP_OK) err = nvs_set_str(h, "kode", k.kode);
  if (err == ESP_OK) err = nvs_set_str(h, "vaert", k.vaertsnavn[0] ? k.vaertsnavn : kStandardVaertsnavn);
  if (err == ESP_OK) err = nvs_set_u8(h, "fast_ip", k.fast_ip ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_u32(h, "ip", k.ip);
  if (err == ESP_OK) err = nvs_set_u32(h, "maske", k.maske);
  if (err == ESP_OK) err = nvs_set_u32(h, "gw", k.gateway);
  if (err == ESP_OK) err = nvs_set_u32(h, "dns", k.dns);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Kunne ikke gemme Wi-Fi-konfiguration: %s", esp_err_to_name(err));
    return false;
  }
  if (s_task) xTaskNotify(s_task, kBitNyKonfig, eSetBits);
  return true;
}

bool scan(std::vector<Netvaerk> &ud, std::string &fejl) {
  static SemaphoreHandle_t scan_mutex = xSemaphoreCreateMutex();
  xSemaphoreTake(scan_mutex, portMAX_DELAY);
  ud.clear();
  wifi_scan_config_t cfg = {};
  cfg.show_hidden = false;
  esp_err_t err = esp_wifi_scan_start(&cfg, true);  // blokerer ca. 2 s
  if (err == ESP_OK) {
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > 30) n = 30;
    std::vector<wifi_ap_record_t> rec(n);
    err = esp_wifi_scan_get_ap_records(&n, rec.data());
    for (uint16_t i = 0; err == ESP_OK && i < n; ++i) {
      if (rec[i].ssid[0] == '\0') continue;
      Netvaerk net;
      net.ssid = reinterpret_cast<const char *>(rec[i].ssid);
      net.rssi = rec[i].rssi;
      net.krypteret = rec[i].authmode != WIFI_AUTH_OPEN;
      // Samme navn fra flere access points (mesh) vises én gang med det stærkeste signal.
      bool dublet = false;
      for (auto &e : ud) {
        if (e.ssid == net.ssid) {
          if (net.rssi > e.rssi) e.rssi = net.rssi;
          dublet = true;
        }
      }
      if (!dublet) ud.push_back(net);
    }
  }
  xSemaphoreGive(scan_mutex);
  if (err != ESP_OK) {
    fejl = err == ESP_ERR_WIFI_STATE ? "Wi-Fi er optaget af et forbindelsesforsøg - prøv igen om lidt"
                                      : std::string("Scanning fejlede: ") + esp_err_to_name(err);
    return false;
  }
  return true;
}

void noed_kode_aendret() {
  if (s_task) xTaskNotify(s_task, kBitNyKode, eSetBits);
}

}  // namespace wifi
