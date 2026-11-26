#include "mqtt.h"

#include <atomic>
#include <cstring>

#include "board_pins.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "ihc_bus.h"
#include "mqtt_client.h"
#include "mqtt_topics.h"
#include "lager.h"
#include "nvs.h"
#include "opsaetning.h"
#include "temperatur.h"
#include "version.h"
#include "wifi.h"

namespace mqtt {
namespace {

const char *TAG = "mqtt";
constexpr const char *kNamespace = "mqtt";

constexpr uint32_t kBitForbundet = 1u << 0;
constexpr uint32_t kBitAfbrudt = 1u << 1;
constexpr uint32_t kBitAendring = 1u << 2;
constexpr uint32_t kBitGenudgiv = 1u << 3;
constexpr uint32_t kBitGenstart = 1u << 4;
constexpr uint32_t kPollMs = 200;

TaskHandle_t s_task = nullptr;
esp_mqtt_client_handle_t s_client = nullptr;

// s_konfig ændres kun af MQTT-tasken, mens klienten er stoppet, så
// event-handleren kan læse den uden lås. s_mutex beskytter s_status,
// s_ny_konfig og læsning fra andre tasks.
SemaphoreHandle_t s_mutex = nullptr;
Konfig s_konfig;
Konfig s_ny_konfig;
Status s_status;

std::atomic<uint16_t> s_udgange_aendret{0};
std::atomic<uint16_t> s_udgange_tving{0};
std::atomic<uint32_t> s_modtaget{0};
// Tidspunkt (ms) hvor discovery skal sendes igen efter ændrede navne/typer; 0 = intet.
std::atomic<int64_t> s_discovery_frist{0};
constexpr int64_t kDiscoveryVentMs = 2000;

// Hvad der sidst er sendt (kun brugt i MQTT-tasken).
bool s_forbundet = false;
uint8_t s_sendt_udgang[ihc::kOutputModules];
bool s_udgang_kendt[ihc::kOutputModules];
uint16_t s_sendt_indgang[ihc::kInputModules];
bool s_indgang_kendt[ihc::kInputModules];
int8_t s_sendt_online[ihc::kInputModules];
// Temperaturfølere: senest sendte værdier pr. indgang.
struct SendtTemp {
  bool kendt = false;
  int8_t online = -1;
  ihc::TemperaturMaaling m;
};
SendtTemp s_sendt_temp[ihc::kInputModules][ihc::kInputsPerModule];
uint32_t s_sendt = 0;

void laes_str(nvs_handle_t h, const char *key, std::string &out) {
  size_t len = 0;
  if (nvs_get_str(h, key, nullptr, &len) != ESP_OK || len == 0) return;
  std::string buf(len, '\0');
  if (nvs_get_str(h, key, buf.data(), &len) == ESP_OK) out.assign(buf.c_str());
}

Konfig laes_konfig() {
  Konfig k;

  nvs_handle_t h;
  if (lager::aaben(kNamespace, NVS_READONLY, &h) == ESP_OK) {
    laes_str(h, "vaert", k.vaert);
    laes_str(h, "bruger", k.bruger);
    laes_str(h, "kode", k.kode);
    laes_str(h, "klient", k.klient_id);
    laes_str(h, "basis", k.basis_topic);
    laes_str(h, "pl_on", k.payload_on);
    laes_str(h, "pl_off", k.payload_off);
    laes_str(h, "disc_pfx", k.discovery_prefix);
    uint8_t u8 = 0;
    uint16_t u16 = 0;
    if (nvs_get_u16(h, "port", &u16) == ESP_OK) k.port = u16;
    if (nvs_get_u8(h, "retain", &u8) == ESP_OK) k.retain = u8 != 0;
    if (nvs_get_u8(h, "qos", &u8) == ESP_OK) k.qos = u8 > 1 ? 1 : u8;
    if (nvs_get_u16(h, "keepalive", &u16) == ESP_OK) k.keepalive = u16;
    if (nvs_get_u8(h, "disc", &u8) == ESP_OK) k.discovery = u8 != 0;
    nvs_close(h);
  }
  return k;
}

std::string klient_id(const Konfig &k) {
  return k.klient_id.empty() ? "ihc-bridge-" + opsaetning::enheds_suffix() : k.klient_id;
}

netvaerk::Enhed enhed() {
  netvaerk::Enhed e;
  e.id = "ihcbridge_" + opsaetning::enheds_suffix();
  e.navn = "IHC Bridge";
  e.version = IHC_VERSION;
  const wifi::Status ws = wifi::status();
  if (ws.ip[0]) e.url = std::string("http://") + ws.ip;
  return e;
}

void saet_fejl(const std::string &fejl) {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_status.fejl = fejl;
  xSemaphoreGive(s_mutex);
}

void publish(const std::string &topic, const std::string &payload, bool retain, int qos) {
  if (!s_client) return;
  if (esp_mqtt_client_publish(s_client, topic.c_str(), payload.data(), payload.size(), qos, retain) >= 0) {
    ++s_sendt;
  }
}

// --- Modtagne beskeder (kører i esp-mqtt's task) ---

void haandter_besked(esp_mqtt_event_handle_t e) {
  // Kun hele beskeder; kommandoer er altid små.
  if (e->current_data_offset != 0 || e->data_len != e->total_data_len) return;
  s_modtaget.fetch_add(1);
  const std::string topic(e->topic, e->topic_len);
  const std::string payload(e->data, e->data_len);

  if (s_konfig.discovery && topic == s_konfig.discovery_prefix + "/status") {
    // Home Assistant er genstartet: send discovery og tilstande igen.
    if (payload == netvaerk::kOnline) xTaskNotify(s_task, kBitGenudgiv, eSetBits);
    return;
  }

  uint32_t modul = 0, kanal = 0;
  if (!netvaerk::parse_udgang_set(s_konfig.basis_topic, topic, modul, kanal)) return;
  // En gemt (retained) kommando ville blive udført igen ved hver genforbindelse.
  if (e->retain) {
    ESP_LOGW(TAG, "Ignorerer gemt (retained) kommando på %s", topic.c_str());
    return;
  }
  if (modul > ihc::kOutputModules || kanal > ihc::kOutputsPerModule) return;
  if (!(opsaetning::aktive_outputmoduler() & (1u << (modul - 1)))) return;

  const auto kmd = netvaerk::parse_kommando(payload, s_konfig.payload_on, s_konfig.payload_off);
  if (kmd == netvaerk::Kommando::kTaend) {
    ihc_bus::set_output(modul - 1, kanal - 1, true);
  } else if (kmd == netvaerk::Kommando::kSluk) {
    ihc_bus::set_output(modul - 1, kanal - 1, false);
  } else if (kmd == netvaerk::Kommando::kSkift) {
    ihc_bus::toggle_output(modul - 1, kanal - 1);
  } else {
    ESP_LOGW(TAG, "Ukendt kommando \"%s\" på %s", payload.c_str(), topic.c_str());
    return;
  }
  // Bekræft tilstanden, også hvis udgangen allerede stod sådan.
  s_udgange_tving.fetch_or(static_cast<uint16_t>(1u << (modul - 1)));
  xTaskNotify(s_task, kBitAendring, eSetBits);
}

void on_mqtt_event(void *, esp_event_base_t, int32_t id, void *data) {
  auto *e = static_cast<esp_mqtt_event_handle_t>(data);
  switch (id) {
    case MQTT_EVENT_CONNECTED:
      xTaskNotify(s_task, kBitForbundet, eSetBits);
      break;
    case MQTT_EVENT_DISCONNECTED:
      xTaskNotify(s_task, kBitAfbrudt, eSetBits);
      break;
    case MQTT_EVENT_DATA:
      haandter_besked(e);
      break;
    case MQTT_EVENT_ERROR:
      if (e->error_handle && e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        saet_fejl("Brokeren afviste forbindelsen (forkert bruger/kode?)");
      } else if (e->error_handle && e->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        saet_fejl("Kan ikke nå brokeren");
      }
      break;
    default:
      break;
  }
}

// --- Udgivelse (kører i MQTT-tasken) ---

void nulstil_sendt() {
  for (auto &k : s_udgang_kendt) k = false;
  for (auto &k : s_indgang_kendt) k = false;
  for (auto &o : s_sendt_online) o = -1;
  for (auto &mod : s_sendt_temp)
    for (auto &t : mod) t = SendtTemp();
}

uint16_t temperatur_maske(uint32_t modul) {  // modul fra 1
  uint16_t maske = 0;
  for (uint32_t c = 1; c <= ihc::kInputsPerModule; ++c) {
    if (temperatur::er_foeler(modul, c)) maske |= 1u << (c - 1);
  }
  return maske;
}

void udgiv_discovery(const Konfig &k, bool fjern_alt) {
  const auto e = enhed();
  const uint16_t aktive_out = fjern_alt ? 0 : opsaetning::aktive_outputmoduler();
  const uint8_t aktive_in = fjern_alt ? 0 : opsaetning::aktive_inputmoduler();
  uint32_t n = 0;
  // Aktive kanaler får en konfiguration under den komponent, deres type
  // svarer til (light/switch/fan). Alle andre komponenter og inaktive
  // moduler får en tom besked, som får Home Assistant til at fjerne enheden.
  // Så forsvinder den gamle enhed automatisk, når typen skiftes.
  for (uint32_t m = 1; m <= ihc::kOutputModules; ++m) {
    const bool aktiv = aktive_out & (1u << (m - 1));
    for (uint32_t c = 1; c <= ihc::kOutputsPerModule; ++c) {
      const auto type = opsaetning::udgangstype(m, c);
      const char *komponent = netvaerk::udgang_komponent(type);
      for (const char *komp : netvaerk::kUdgangsKomponenter) {
        const bool denne = aktiv && strcmp(komp, komponent) == 0;
        publish(netvaerk::discovery_topic_udgang(k.discovery_prefix, e, komp, m, c),
                denne ? netvaerk::discovery_udgang(k.basis_topic, e, m, c, opsaetning::udgangsnavn(m, c), type,
                                                   k.payload_on, k.payload_off)
                      : std::string(),
                true, 0);
        if (++n % 16 == 0) vTaskDelay(1);  // giv netværket luft
      }
    }
  }
  for (uint32_t m = 1; m <= ihc::kInputModules; ++m) {
    const bool aktiv = aktive_in & (1u << (m - 1));
    for (uint32_t c = 1; c <= ihc::kInputsPerModule; ++c) {
      // Almindelige indgange er binære sensorer. En temperaturføler er i
      // stedet op til tre sensorer: temperatur, og fugt/gulv, hvis føleren
      // har sendt dem.
      const auto type = opsaetning::indgangstype(m, c);
      const bool temp = type == netvaerk::IndgangsType::kTemperatur;
      const std::string navn = opsaetning::indgangsnavn(m, c);
      publish(netvaerk::discovery_topic_indgang(k.discovery_prefix, e, m, c),
              aktiv && !temp ? netvaerk::discovery_indgang(k.basis_topic, e, m, c, navn, type, k.payload_on, k.payload_off)
                             : std::string(),
              true, 0);
      temperatur::Foeler f;
      if (temp) temperatur::hent(m, c, f);
      for (size_t i = 0; i < 3; ++i) {
        const auto &felt = netvaerk::kTempFelter[i];
        const bool med = aktiv && temp && (i == 0 || (i == 1 && f.set_fugt) || (i == 2 && f.set_gulv));
        publish(netvaerk::discovery_topic_temp(k.discovery_prefix, e, m, c, felt),
                med ? netvaerk::discovery_temp(k.basis_topic, e, m, c, navn, felt) : std::string(), true, 0);
      }
      if (++n % 4 == 0) vTaskDelay(1);
    }
  }
  ESP_LOGI(TAG, "Home Assistant-discovery %s", fjern_alt ? "fjernet" : "sendt");
}

// Sender nye værdier fra temperaturfølerne på et inputmodul (modul fra 1).
void udgiv_temperaturer(uint32_t modul) {
  const Konfig &k = s_konfig;
  for (uint32_t c = 1; c <= ihc::kInputsPerModule; ++c) {
    temperatur::Foeler f;
    if (!temperatur::er_foeler(modul, c) || !temperatur::hent(modul, c, f)) continue;
    SendtTemp &s = s_sendt_temp[modul - 1][c - 1];
    if (s.online != (f.online ? 1 : 0)) {
      publish(netvaerk::topic_temp_status(k.basis_topic, modul, c), f.online ? netvaerk::kOnline : netvaerk::kOffline,
              true, k.qos);
      s.online = f.online ? 1 : 0;
    }
    if (!f.har_maaling) continue;
    const auto &m = f.maaling;
    if (!s.kendt || m.temperatur != s.m.temperatur) {
      publish(netvaerk::topic_temp(k.basis_topic, modul, c, "temperature"), netvaerk::tiendedele(m.temperatur), true, k.qos);
    }
    if (m.har_fugt && (!s.kendt || m.fugt != s.m.fugt)) {
      publish(netvaerk::topic_temp(k.basis_topic, modul, c, "humidity"), netvaerk::tiendedele(m.fugt), true, k.qos);
    }
    if (m.har_gulv && (!s.kendt || m.gulv != s.m.gulv)) {
      publish(netvaerk::topic_temp(k.basis_topic, modul, c, "floor"), netvaerk::tiendedele(m.gulv), true, k.qos);
    }
    s.m = m;
    s.kendt = true;
  }
}

// Sender alle tilstande, der er ændret siden sidst (eller aldrig sendt).
void udgiv_aendringer() {
  const Konfig &k = s_konfig;
  const uint16_t aktive_out = opsaetning::aktive_outputmoduler();
  const uint8_t aktive_in = opsaetning::aktive_inputmoduler();
  s_udgange_aendret.exchange(0);
  const uint16_t tving = s_udgange_tving.exchange(0);

  for (uint32_t m = 0; m < ihc::kOutputModules; ++m) {
    if (!(aktive_out & (1u << m))) continue;
    const uint8_t v = ihc_bus::output_module(m);
    uint8_t diff = s_udgang_kendt[m] ? static_cast<uint8_t>(v ^ s_sendt_udgang[m]) : 0xFF;
    if (tving & (1u << m)) diff = 0xFF;
    for (uint32_t c = 0; c < ihc::kOutputsPerModule; ++c) {
      if (diff & (1u << c)) {
        publish(netvaerk::topic_udgang(k.basis_topic, m + 1, c + 1),
                (v & (1u << c)) ? k.payload_on : k.payload_off, k.retain, k.qos);
      }
    }
    s_sendt_udgang[m] = v;
    s_udgang_kendt[m] = true;
  }

  auto &rx = ihc_bus::modtager();
  for (uint32_t m = 0; m < ihc::kInputModules; ++m) {
    if (!(aktive_in & (1u << m))) continue;
    const bool online = rx.online(m);
    if (s_sendt_online[m] != (online ? 1 : 0)) {
      publish(netvaerk::topic_indgangsmodul_status(k.basis_topic, m + 1),
              online ? netvaerk::kOnline : netvaerk::kOffline, true, k.qos);
      s_sendt_online[m] = online ? 1 : 0;
      if (online) s_indgang_kendt[m] = false;  // send hele tilstanden efter genforbindelse
    }
    udgiv_temperaturer(m + 1);
    if (!online) continue;
    const uint16_t v = rx.state(m);
    uint16_t diff = s_indgang_kendt[m] ? static_cast<uint16_t>(v ^ s_sendt_indgang[m]) : 0xFFFF;
    diff &= ~temperatur_maske(m + 1);  // temperaturfølere sendes som temperatur, ikke ON/OFF
    for (uint32_t c = 0; c < ihc::kInputsPerModule; ++c) {
      if (diff & (1u << c)) {
        publish(netvaerk::topic_indgang(k.basis_topic, m + 1, c + 1),
                (v & (1u << c)) ? k.payload_on : k.payload_off, k.retain, k.qos);
      }
    }
    s_sendt_indgang[m] = v;
    s_indgang_kendt[m] = true;
  }
}

void ved_forbindelse() {
  const Konfig &k = s_konfig;
  s_forbundet = true;
  saet_fejl("");
  ESP_LOGI(TAG, "Forbundet til broker %s:%u", k.vaert.c_str(), (unsigned)k.port);
  publish(netvaerk::topic_status(k.basis_topic), netvaerk::kOnline, true, 1);
  esp_mqtt_client_subscribe(s_client, netvaerk::topic_udgang_set_alle(k.basis_topic).c_str(), 1);
  if (k.discovery) {
    esp_mqtt_client_subscribe(s_client, (k.discovery_prefix + "/status").c_str(), 1);
    udgiv_discovery(k, false);
  }
  nulstil_sendt();
  udgiv_aendringer();
}

// Strenge til klientkonfigurationen skal leve, så længe klienten gør.
std::string s_uri, s_klient_id, s_lwt_topic;

void opret_klient() {
  const Konfig &k = s_konfig;
  if (k.vaert.empty()) {
    ESP_LOGW(TAG, "Ingen MQTT-broker sat op");
    return;
  }
  s_uri = "mqtt://" + k.vaert + ":" + std::to_string(k.port);
  s_klient_id = klient_id(k);
  s_lwt_topic = netvaerk::topic_status(k.basis_topic);

  esp_mqtt_client_config_t cfg = {};
  cfg.broker.address.uri = s_uri.c_str();
  cfg.credentials.client_id = s_klient_id.c_str();
  if (!k.bruger.empty()) cfg.credentials.username = k.bruger.c_str();
  if (!k.kode.empty()) cfg.credentials.authentication.password = k.kode.c_str();
  cfg.session.keepalive = k.keepalive;
  cfg.session.last_will.topic = s_lwt_topic.c_str();
  cfg.session.last_will.msg = netvaerk::kOffline;
  cfg.session.last_will.qos = 1;
  cfg.session.last_will.retain = 1;
  cfg.network.reconnect_timeout_ms = 5000;
  cfg.buffer.size = 2048;  // discovery-beskeder er ca. 600 byte
  cfg.task.stack_size = 6144;

  s_client = esp_mqtt_client_init(&cfg);
  if (!s_client) {
    saet_fejl("Kunne ikke oprette MQTT-klient");
    return;
  }
  esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, on_mqtt_event, nullptr);
  esp_mqtt_client_start(s_client);
  ESP_LOGI(TAG, "Forbinder til %s som %s", s_uri.c_str(), s_klient_id.c_str());
}

void stop_klient() {
  if (!s_client) return;
  esp_mqtt_client_stop(s_client);
  esp_mqtt_client_destroy(s_client);
  s_client = nullptr;
  s_forbundet = false;
}

void genstart(const Konfig &ny) {
  const Konfig gammel = s_konfig;
  if (s_forbundet) {
    // Ryd op i Home Assistant, hvis discovery slås fra eller flyttes.
    const bool flyttet = gammel.discovery_prefix != ny.discovery_prefix || gammel.basis_topic != ny.basis_topic;
    if (gammel.discovery && (!ny.discovery || flyttet)) udgiv_discovery(gammel, true);
    // En planlagt afbrydelse udløser ikke last will; meld selv offline.
    publish(netvaerk::topic_status(gammel.basis_topic), netvaerk::kOffline, true, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  stop_klient();
  s_konfig = ny;
  opret_klient();
}

void opdater_status() {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_status.aktiveret = !s_konfig.vaert.empty();
  s_status.forbundet = s_forbundet;
  s_status.sendt = s_sendt;
  s_status.modtaget = s_modtaget.load();
  xSemaphoreGive(s_mutex);
}

void mqtt_task(void *) {
  nulstil_sendt();
  opret_klient();
  for (;;) {
    uint32_t bits = 0;
    xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(kPollMs));

    if (bits & kBitGenstart) {
      xSemaphoreTake(s_mutex, portMAX_DELAY);
      const Konfig ny = s_ny_konfig;
      xSemaphoreGive(s_mutex);
      genstart(ny);
    }
    if (bits & kBitAfbrudt) {
      if (s_forbundet) ESP_LOGW(TAG, "Forbindelsen til brokeren er tabt");
      s_forbundet = false;
    }
    if (bits & kBitForbundet) ved_forbindelse();
    if ((bits & kBitGenudgiv) && s_forbundet) {
      if (s_konfig.discovery) udgiv_discovery(s_konfig, false);
      nulstil_sendt();
    }
    // Navne, typer eller aktive moduler er ændret: send discovery igen,
    // når der har været ro i 2 s.
    const int64_t frist = s_discovery_frist.load();
    if (frist && esp_timer_get_time() / 1000 >= frist) {
      s_discovery_frist = 0;
      if (s_forbundet && s_konfig.discovery) udgiv_discovery(s_konfig, false);
      nulstil_sendt();  // nye aktive moduler skal også have deres tilstande
    }
    // Også uden besked: fanger inputmoduler, der går online/offline.
    if (s_forbundet) udgiv_aendringer();
    opdater_status();
  }
}

}  // namespace

void start() {
  s_mutex = xSemaphoreCreateMutex();
  s_konfig = laes_konfig();
  xTaskCreatePinnedToCore(mqtt_task, "mqtt", 6144, nullptr, 6, &s_task, 0);
}

Status status() {
  Status st;
  if (!s_mutex) return st;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  st = s_status;
  xSemaphoreGive(s_mutex);
  return st;
}

Konfig hent_konfig(bool skjul_kode) {
  Konfig k = laes_konfig();
  if (skjul_kode) k.kode.clear();
  return k;
}

bool gem_konfig(const Konfig &k, bool behold_kode, std::string &fejl) {
  if (!netvaerk::basis_topic_gyldig(k.basis_topic)) {
    fejl = "Ugyldigt basis-topic";
    return false;
  }
  if (k.discovery && !netvaerk::basis_topic_gyldig(k.discovery_prefix)) {
    fejl = "Ugyldigt discovery-prefix";
    return false;
  }
  if (k.payload_on.empty() || k.payload_off.empty() || k.payload_on == k.payload_off) {
    fejl = "Payloads for tændt og slukket skal være forskellige og må ikke være tomme";
    return false;
  }
  if (k.port == 0) {
    fejl = "Ugyldig port";
    return false;
  }

  Konfig ny = k;
  if (behold_kode) ny.kode = laes_konfig().kode;
  if (ny.qos > 1) ny.qos = 1;

  nvs_handle_t h;
  if (lager::aaben(kNamespace, NVS_READWRITE, &h) != ESP_OK) {
    fejl = "Kunne ikke åbne lager";
    return false;
  }
  esp_err_t err = ESP_OK;
  auto str = [&](const char *key, const std::string &v) {
    if (err == ESP_OK) err = nvs_set_str(h, key, v.c_str());
  };
  str("vaert", ny.vaert);
  str("bruger", ny.bruger);
  str("kode", ny.kode);
  str("klient", ny.klient_id);
  str("basis", ny.basis_topic);
  str("pl_on", ny.payload_on);
  str("pl_off", ny.payload_off);
  str("disc_pfx", ny.discovery_prefix);
  if (err == ESP_OK) err = nvs_set_u16(h, "port", ny.port);
  if (err == ESP_OK) err = nvs_set_u8(h, "retain", ny.retain ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_u8(h, "qos", ny.qos);
  if (err == ESP_OK) err = nvs_set_u16(h, "keepalive", ny.keepalive);
  if (err == ESP_OK) err = nvs_set_u8(h, "disc", ny.discovery ? 1 : 0);
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  if (err != ESP_OK) {
    fejl = std::string("Kunne ikke gemme: ") + esp_err_to_name(err);
    return false;
  }

  xSemaphoreTake(s_mutex, portMAX_DELAY);
  s_ny_konfig = ny;
  xSemaphoreGive(s_mutex);
  if (s_task) xTaskNotify(s_task, kBitGenstart, eSetBits);
  return true;
}

void udgange_aendret(uint16_t moduler) {
  s_udgange_aendret.fetch_or(moduler);
  if (s_task) xTaskNotify(s_task, kBitAendring, eSetBits);
}

void konfiguration_aendret() {
  s_discovery_frist = esp_timer_get_time() / 1000 + kDiscoveryVentMs;
}

void indgange_aendret(uint8_t) {
  if (s_task) xTaskNotify(s_task, kBitAendring, eSetBits);
}

}  // namespace mqtt
