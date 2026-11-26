#include "web.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "board_pins.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ihc_bus.h"
#include "kanaltyper.h"
#include "mqtt.h"
#include "opsaetning.h"
#include "sikkerhed.h"
#include "temperatur.h"
#include "tilstand.h"
#include "version.h"
#include "logbog.h"
#include "web_ui_data.h"
#include "wifi.h"

namespace web {
namespace {

const char *TAG = "web";
constexpr size_t kMaxBody = 64 * 1024;  // JSON-forespørgsler (import kan fylde lidt)
constexpr uint32_t kPushPollMs = 200;
constexpr const char *kCookie = "ihc_session";

httpd_handle_t s_server = nullptr;
TaskHandle_t s_push_task = nullptr;
std::atomic<bool> s_ny_klient{false};  // en ny browser skal have hele tilstanden

// ---------------------------------------------------------------------------
// Hjælpere
// ---------------------------------------------------------------------------

using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
Json ny_json() { return Json(cJSON_CreateObject(), cJSON_Delete); }

void saet_sikkerheds_headere(httpd_req_t *req) {
  httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
  httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
  httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
}

esp_err_t send_json(httpd_req_t *req, const char *status, cJSON *json) {
  char *tekst = cJSON_PrintUnformatted(json);
  saet_sikkerheds_headere(req);
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  const esp_err_t err = httpd_resp_send(req, tekst ? tekst : "{}", HTTPD_RESP_USE_STRLEN);
  cJSON_free(tekst);
  return err;
}

esp_err_t send_ok(httpd_req_t *req) {
  auto j = ny_json();
  cJSON_AddBoolToObject(j.get(), "ok", true);
  return send_json(req, "200 OK", j.get());
}

esp_err_t send_fejl(httpd_req_t *req, const char *status, const std::string &besked) {
  auto j = ny_json();
  cJSON_AddBoolToObject(j.get(), "ok", false);
  cJSON_AddStringToObject(j.get(), "fejl", besked.c_str());
  return send_json(req, status, j.get());
}

esp_err_t omdiriger(httpd_req_t *req, const char *til) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", til);
  return httpd_resp_send(req, nullptr, 0);
}

// Læser JSON-body. Kræver Content-Type application/json, så en anden
// hjemmeside ikke kan sende en almindelig formular til enheden.
Json laes_json(httpd_req_t *req, std::string &fejl) {
  char type[48] = {};
  if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
      strncmp(type, "application/json", 16) != 0) {
    fejl = "Forventede JSON";
    return Json(nullptr, cJSON_Delete);
  }
  if (req->content_len == 0 || req->content_len > kMaxBody) {
    fejl = "Ugyldig størrelse";
    return Json(nullptr, cJSON_Delete);
  }
  std::string body(req->content_len, '\0');
  size_t laest = 0;
  while (laest < req->content_len) {
    const int r = httpd_req_recv(req, body.data() + laest, req->content_len - laest);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (r <= 0) {
      fejl = "Afbrudt";
      return Json(nullptr, cJSON_Delete);
    }
    laest += r;
  }
  Json j(cJSON_ParseWithLength(body.data(), body.size()), cJSON_Delete);
  if (!j) fejl = "Ugyldig JSON";
  return j;
}

std::string json_str(const cJSON *j, const char *navn, const std::string &standard = "") {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, navn);
  return cJSON_IsString(v) && v->valuestring ? v->valuestring : standard;
}

bool json_int(const cJSON *j, const char *navn, int &ud) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, navn);
  if (!cJSON_IsNumber(v)) return false;
  ud = v->valueint;
  return true;
}

bool json_bool(const cJSON *j, const char *navn, bool standard) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, navn);
  return cJSON_IsBool(v) ? cJSON_IsTrue(v) : standard;
}

std::string laes_cookie(httpd_req_t *req) {
  char buf[80] = {};
  size_t len = sizeof(buf);
  if (httpd_req_get_cookie_val(req, kCookie, buf, &len) != ESP_OK) return "";
  return buf;
}

void saet_cookie(httpd_req_t *req, const std::string &token, std::string &holder) {
  // HttpOnly: JavaScript kan ikke læse den. SameSite=Strict: sendes ikke med
  // forespørgsler fra andre hjemmesider.
  holder = std::string(kCookie) + "=" + token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=604800";
  httpd_resp_set_hdr(req, "Set-Cookie", holder.c_str());
}

// Returnerer true, hvis forespørgslen må fortsætte. Ellers er der svaret.
bool kraev_login(httpd_req_t *req) {
  if (!sikkerhed::har_kode()) {
    send_fejl(req, "409 Conflict", "Enheden er ikke sat op endnu");
    return false;
  }
  if (!sikkerhed::session_gyldig(laes_cookie(req))) {
    send_fejl(req, "401 Unauthorized", "Log ind først");
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// JSON-opbygning
// ---------------------------------------------------------------------------

void tilfoej_wifi_status(cJSON *j) {
  const wifi::Status ws = wifi::status();
  cJSON *w = cJSON_AddObjectToObject(j, "wifi");
  cJSON_AddBoolToObject(w, "forbundet", ws.forbundet);
  cJSON_AddStringToObject(w, "ssid", ws.ssid);
  cJSON_AddStringToObject(w, "ip", ws.ip);
  cJSON_AddNumberToObject(w, "rssi", ws.rssi);
  cJSON_AddStringToObject(w, "vaertsnavn", ws.vaertsnavn);
  const char *ap = ws.ap == netvaerk::ApTilstand::kOpsaetning      ? "opsaetning"
                   : ws.ap == netvaerk::ApTilstand::kNoedbetjening ? "noedbetjening"
                                                                   : "slukket";
  cJSON_AddStringToObject(w, "ap", ap);
  cJSON_AddNumberToObject(w, "forsoeg", ws.forsoeg);
  cJSON_AddNumberToObject(w, "naeste_forsoeg_s", static_cast<double>(ws.ms_til_naeste / 1000));
}

// Temperaturføler som JSON: værdier i hele grader med én decimal, eller null.
cJSON *temperatur_json(uint32_t m, uint32_t k) {
  temperatur::Foeler f;
  if (!temperatur::hent(m, k, f)) return cJSON_CreateNull();
  cJSON *o = cJSON_CreateObject();
  cJSON_AddBoolToObject(o, "online", f.online);
  if (f.har_maaling) {
    cJSON_AddNumberToObject(o, "temperatur", f.maaling.temperatur / 10.0);
    if (f.maaling.har_fugt) cJSON_AddNumberToObject(o, "fugt", f.maaling.fugt / 10.0);
    if (f.maaling.har_gulv) cJSON_AddNumberToObject(o, "gulv", f.maaling.gulv / 10.0);
    cJSON_AddNumberToObject(o, "alder_s", f.alder_s);
  }
  return o;
}

cJSON *io_json() {
  cJSON *j = cJSON_CreateObject();
  const uint16_t aktive_out = opsaetning::aktive_outputmoduler();
  const uint8_t aktive_in = opsaetning::aktive_inputmoduler();

  cJSON *udgange = cJSON_AddArrayToObject(j, "udgange");
  for (uint32_t m = 1; m <= ihc::kOutputModules; ++m) {
    cJSON *mod = cJSON_CreateObject();
    cJSON_AddNumberToObject(mod, "modul", m);
    cJSON_AddBoolToObject(mod, "aktiv", (aktive_out >> (m - 1)) & 1);
    cJSON_AddNumberToObject(mod, "tilstand", ihc_bus::output_module(m - 1));
    cJSON *kanaler = cJSON_AddArrayToObject(mod, "kanaler");
    for (uint32_t k = 1; k <= ihc::kOutputsPerModule; ++k) {
      cJSON *kan = cJSON_CreateObject();
      cJSON_AddStringToObject(kan, "navn", opsaetning::udgangsnavn(m, k).c_str());
      cJSON_AddBoolToObject(kan, "eget_navn", opsaetning::har_eget_udgangsnavn(m, k));
      cJSON_AddStringToObject(kan, "type", netvaerk::udgangstype_navn(opsaetning::udgangstype(m, k)));
      cJSON_AddItemToArray(kanaler, kan);
    }
    cJSON_AddItemToArray(udgange, mod);
  }

  auto &rx = ihc_bus::modtager();
  cJSON *indgange = cJSON_AddArrayToObject(j, "indgange");
  for (uint32_t m = 1; m <= ihc::kInputModules; ++m) {
    cJSON *mod = cJSON_CreateObject();
    cJSON_AddNumberToObject(mod, "modul", m);
    cJSON_AddBoolToObject(mod, "aktiv", (aktive_in >> (m - 1)) & 1);
    cJSON_AddBoolToObject(mod, "online", rx.online(m - 1));
    cJSON_AddNumberToObject(mod, "tilstand", rx.state(m - 1));
    cJSON *kanaler = cJSON_AddArrayToObject(mod, "kanaler");
    for (uint32_t k = 1; k <= ihc::kInputsPerModule; ++k) {
      cJSON *kan = cJSON_CreateObject();
      cJSON_AddStringToObject(kan, "navn", opsaetning::indgangsnavn(m, k).c_str());
      cJSON_AddBoolToObject(kan, "eget_navn", opsaetning::har_eget_indgangsnavn(m, k));
      const auto type = opsaetning::indgangstype(m, k);
      cJSON_AddStringToObject(kan, "type", netvaerk::indgangstype_navn(type));
      if (type == netvaerk::IndgangsType::kTemperatur) cJSON_AddItemToObject(kan, "temperatur", temperatur_json(m, k));
      cJSON_AddItemToArray(kanaler, kan);
    }
    cJSON_AddItemToArray(indgange, mod);
  }
  return j;
}

// ---------------------------------------------------------------------------
// Web-UI (én side, der selv viser opsætning, login eller oversigt)
// ---------------------------------------------------------------------------

esp_err_t side_ui(httpd_req_t *req) {
  // Browseren har allerede den nyeste udgave: svar kort.
  char etag[24] = {};
  if (httpd_req_get_hdr_value_str(req, "If-None-Match", etag, sizeof(etag)) == ESP_OK &&
      strcmp(etag, kWebUiEtag) == 0) {
    httpd_resp_set_status(req, "304 Not Modified");
    return httpd_resp_send(req, nullptr, 0);
  }
  saet_sikkerheds_headere(req);
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(req, "ETag", kWebUiEtag);
  return httpd_resp_send(req, reinterpret_cast<const char *>(kWebUiGz), kWebUiGzLen);
}

esp_err_t side_opsaetning(httpd_req_t *req) { return omdiriger(req, "/"); }

// ---------------------------------------------------------------------------
// Opsætning og login
// ---------------------------------------------------------------------------

esp_err_t api_opsaetning_get(httpd_req_t *req) {
  auto j = ny_json();
  cJSON_AddBoolToObject(j.get(), "opsat", sikkerhed::har_kode());
  cJSON_AddStringToObject(j.get(), "version", IHC_VERSION);
  tilfoej_wifi_status(j.get());
  return send_json(req, "200 OK", j.get());
}

esp_err_t api_opsaetning_post(httpd_req_t *req) {
  if (sikkerhed::har_kode()) return send_fejl(req, "409 Conflict", "Enheden er allerede sat op");
  std::string fejl;
  auto j = laes_json(req, fejl);
  if (!j) return send_fejl(req, "400 Bad Request", fejl);

  const std::string kode = json_str(j.get(), "kode");
  const std::string ssid = json_str(j.get(), "wifi_ssid");
  const std::string wifi_kode = json_str(j.get(), "wifi_kode");
  if (ssid.size() > 32 || wifi_kode.size() > 64) return send_fejl(req, "400 Bad Request", "Ugyldigt Wi-Fi");
  if (!sikkerhed::saet_kode(kode, fejl)) return send_fejl(req, "400 Bad Request", fejl);

  // Tomt netværksnavn betyder: behold det nuværende (f.eks. efter nulstilling
  // af adgangskoden med BOOT-knappen).
  if (!ssid.empty()) {
    wifi::Konfig k;
    wifi::hent_konfig(k, false);
    strncpy(k.ssid, ssid.c_str(), sizeof(k.ssid) - 1);
    k.ssid[ssid.size()] = '\0';
    memset(k.kode, 0, sizeof(k.kode));
    strncpy(k.kode, wifi_kode.c_str(), sizeof(k.kode) - 1);
    wifi::gem_konfig(k);
  }
  ESP_LOGI(TAG, "Førstegangsopsætning gennemført");
  std::string cookie;
  saet_cookie(req, sikkerhed::ny_session(), cookie);
  return send_ok(req);
}

esp_err_t api_wifi_scan(httpd_req_t *req) {
  if (sikkerhed::har_kode() && !kraev_login(req)) return ESP_OK;
  std::vector<wifi::Netvaerk> net;
  std::string fejl;
  if (!wifi::scan(net, fejl)) return send_fejl(req, "503 Service Unavailable", fejl);
  auto j = ny_json();
  cJSON *arr = cJSON_AddArrayToObject(j.get(), "netvaerk");
  for (const auto &n : net) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "ssid", n.ssid.c_str());
    cJSON_AddNumberToObject(o, "rssi", n.rssi);
    cJSON_AddBoolToObject(o, "krypteret", n.krypteret);
    cJSON_AddItemToArray(arr, o);
  }
  return send_json(req, "200 OK", j.get());
}

esp_err_t api_login(httpd_req_t *req) {
  if (!sikkerhed::har_kode()) return send_fejl(req, "409 Conflict", "Enheden er ikke sat op endnu");
  std::string fejl;
  auto j = laes_json(req, fejl);
  if (!j) return send_fejl(req, "400 Bad Request", fejl);
  std::string token;
  uint32_t vent_s = 0;
  switch (sikkerhed::login(json_str(j.get(), "kode"), token, vent_s)) {
    case sikkerhed::LoginResultat::kOk: {
      std::string cookie;
      saet_cookie(req, token, cookie);
      return send_ok(req);
    }
    case sikkerhed::LoginResultat::kVent:
      return send_fejl(req, "429 Too Many Requests",
                       "For mange forkerte forsøg. Vent " + std::to_string(vent_s) + " sekunder.");
    default:
      return send_fejl(req, "401 Unauthorized", "Forkert adgangskode");
  }
}

esp_err_t api_logud(httpd_req_t *req) {
  sikkerhed::log_ud(laes_cookie(req));
  httpd_resp_set_hdr(req, "Set-Cookie", "ihc_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
  return send_ok(req);
}

esp_err_t api_kode(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  std::string fejl;
  auto j = laes_json(req, fejl);
  if (!j) return send_fejl(req, "400 Bad Request", fejl);
  if (!sikkerhed::skift_kode(json_str(j.get(), "gammel"), json_str(j.get(), "ny"), fejl)) {
    return send_fejl(req, "400 Bad Request", fejl);
  }
  luk_websockets();
  // Denne browser forbliver logget ind med en ny session.
  std::string cookie;
  saet_cookie(req, sikkerhed::ny_session(), cookie);
  return send_ok(req);
}

// ---------------------------------------------------------------------------
// Status og I/O
// ---------------------------------------------------------------------------

esp_err_t api_status(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  auto j = ny_json();
  cJSON *sys = cJSON_AddObjectToObject(j.get(), "system");
  cJSON_AddStringToObject(sys, "version", IHC_VERSION);
  cJSON_AddNumberToObject(sys, "oppetid_s", static_cast<double>(esp_timer_get_time() / 1000000));
  cJSON_AddNumberToObject(sys, "fri_ram", esp_get_free_internal_heap_size());
  cJSON_AddNumberToObject(sys, "fri_psram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  cJSON_AddStringToObject(sys, "enheds_id", opsaetning::enheds_suffix().c_str());
  esp_ota_img_states_t ota = ESP_OTA_IMG_UNDEFINED;
  esp_ota_get_state_partition(esp_ota_get_running_partition(), &ota);
  cJSON_AddBoolToObject(sys, "firmware_afventer_bekraeftelse", ota == ESP_OTA_IMG_PENDING_VERIFY);

  tilfoej_wifi_status(j.get());

  const mqtt::Status ms = mqtt::status();
  cJSON *mq = cJSON_AddObjectToObject(j.get(), "mqtt");
  cJSON_AddBoolToObject(mq, "aktiveret", ms.aktiveret);
  cJSON_AddBoolToObject(mq, "forbundet", ms.forbundet);
  cJSON_AddStringToObject(mq, "fejl", ms.fejl.c_str());
  cJSON_AddNumberToObject(mq, "sendt", ms.sendt);
  cJSON_AddNumberToObject(mq, "modtaget", ms.modtaget);

  cJSON *bus = cJSON_AddObjectToObject(j.get(), "bus");
  cJSON_AddNumberToObject(bus, "frames_sendt", ihc_bus::sender().frames_sent());
  cJSON_AddNumberToObject(bus, "max_isr_us", ihc_bus::isr_max_us());
  cJSON *mods = cJSON_AddArrayToObject(bus, "inputmoduler");
  auto &rx = ihc_bus::modtager();
  for (uint32_t m = 0; m < ihc::kInputModules; ++m) {
    const auto s = rx.stats(m);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "modul", m + 1);
    cJSON_AddBoolToObject(o, "online", rx.online(m));
    cJSON_AddNumberToObject(o, "frames_ok", s.frames_ok);
    cJSON_AddNumberToObject(o, "paritet16", s.parity16_match);
    cJSON_AddNumberToObject(o, "paritet8", s.parity8_match);
    cJSON_AddNumberToObject(o, "paritetsfejl", s.parity_errors);
    cJSON_AddNumberToObject(o, "pulsfejl", s.framing_errors);
    cJSON_AddItemToArray(mods, o);
  }
  cJSON *foelere = cJSON_AddArrayToObject(bus, "temperaturfoelere");
  for (uint32_t m = 1; m <= ihc::kInputModules; ++m) {
    for (uint32_t k = 1; k <= ihc::kInputsPerModule; ++k) {
      temperatur::Foeler f;
      if (!temperatur::hent(m, k, f)) continue;
      cJSON *o = cJSON_CreateObject();
      cJSON_AddNumberToObject(o, "modul", m);
      cJSON_AddNumberToObject(o, "kanal", k);
      cJSON_AddBoolToObject(o, "online", f.online);
      cJSON_AddNumberToObject(o, "gyldige", f.gyldige);
      cJSON_AddNumberToObject(o, "fejl", f.fejl);
      if (f.har_maaling) cJSON_AddNumberToObject(o, "alder_s", f.alder_s);
      cJSON_AddItemToArray(foelere, o);
    }
  }
  cJSON_AddNumberToObject(bus, "temperatur_koe_tabt", ihc_bus::temp_koe_tabt());
  return send_json(req, "200 OK", j.get());
}

esp_err_t api_io(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  Json j(io_json(), cJSON_Delete);
  return send_json(req, "200 OK", j.get());
}

// Fælles for POST /api/udgang og WebSocket-kommandoen "udgang".
bool udfoer_udgang(const cJSON *j, std::string &fejl) {
  int m = 0, k = 0;
  if (!json_int(j, "modul", m) || !json_int(j, "kanal", k) || m < 1 || m > (int)ihc::kOutputModules || k < 1 ||
      k > (int)ihc::kOutputsPerModule) {
    fejl = "Ugyldigt modul eller kanal";
    return false;
  }
  const cJSON *t = cJSON_GetObjectItemCaseSensitive(j, "tilstand");
  if (cJSON_IsBool(t)) {
    ihc_bus::set_output(m - 1, k - 1, cJSON_IsTrue(t));
  } else if (cJSON_IsString(t) && strcmp(t->valuestring, "skift") == 0) {
    ihc_bus::toggle_output(m - 1, k - 1);
  } else {
    fejl = "tilstand skal være true, false eller \"skift\"";
    return false;
  }
  return true;
}

esp_err_t api_udgang(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  std::string fejl;
  auto j = laes_json(req, fejl);
  if (!j || !udfoer_udgang(j.get(), fejl)) return send_fejl(req, "400 Bad Request", fejl);
  return send_ok(req);
}

esp_err_t api_kanal(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  std::string fejl;
  auto j = laes_json(req, fejl);
  if (!j) return send_fejl(req, "400 Bad Request", fejl);
  int m = 0, k = 0;
  if (!json_int(j.get(), "modul", m) || !json_int(j.get(), "kanal", k)) {
    return send_fejl(req, "400 Bad Request", "modul og kanal mangler");
  }
  const std::string retning = json_str(j.get(), "retning");
  const std::string navn = json_str(j.get(), "navn");
  const std::string type = json_str(j.get(), "type");
  if (navn.size() > opsaetning::kMaxNavn) return send_fejl(req, "400 Bad Request", "Navnet er for langt (højst 32 tegn)");
  bool ok = false;
  if (retning == "udgang") {
    netvaerk::UdgangsType t;
    if (!netvaerk::udgangstype_fra_navn(type, t)) return send_fejl(req, "400 Bad Request", "Ukendt udgangstype");
    ok = opsaetning::gem_udgang(m, k, navn, t);
  } else if (retning == "indgang") {
    netvaerk::IndgangsType t;
    if (!netvaerk::indgangstype_fra_navn(type, t)) return send_fejl(req, "400 Bad Request", "Ukendt indgangstype");
    ok = opsaetning::gem_indgang(m, k, navn, t);
  } else {
    return send_fejl(req, "400 Bad Request", "retning skal være \"udgang\" eller \"indgang\"");
  }
  return ok ? send_ok(req) : send_fejl(req, "400 Bad Request", "Kunne ikke gemme kanalen");
}

esp_err_t api_moduler(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  std::string fejl;
  auto j = laes_json(req, fejl);
  if (!j) return send_fejl(req, "400 Bad Request", fejl);
  uint16_t out = 0;
  uint8_t in = 0;
  const cJSON *v;
  cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(j.get(), "udgange")) {
    if (cJSON_IsNumber(v) && v->valueint >= 1 && v->valueint <= (int)ihc::kOutputModules) {
      out |= static_cast<uint16_t>(1u << (v->valueint - 1));
    }
  }
  cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(j.get(), "indgange")) {
    if (cJSON_IsNumber(v) && v->valueint >= 1 && v->valueint <= (int)ihc::kInputModules) {
      in |= static_cast<uint8_t>(1u << (v->valueint - 1));
    }
  }
  return opsaetning::gem_aktive_moduler(out, in) ? send_ok(req)
                                                 : send_fejl(req, "500 Internal Server Error", "Kunne ikke gemme");
}

// ---------------------------------------------------------------------------
// Regler
// ---------------------------------------------------------------------------

esp_err_t api_regler_get(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  std::vector<ihc::Regel> regler(ihc::Regeltabel::kMaxRegler);
  const size_t n = ihc_bus::regler(regler.data(), regler.size());
  auto j = ny_json();
  cJSON *arr = cJSON_AddArrayToObject(j.get(), "regler");
  for (size_t i = 0; i < n; ++i) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "kip");
    cJSON_AddNumberToObject(o, "ind_modul", regler[i].in_module + 1);
    cJSON_AddNumberToObject(o, "ind_kanal", regler[i].in_channel + 1);
    cJSON_AddNumberToObject(o, "ud_modul", regler[i].out_module + 1);
    cJSON_AddNumberToObject(o, "ud_kanal", regler[i].out_channel + 1);
    cJSON_AddItemToArray(arr, o);
  }
  return send_json(req, "200 OK", j.get());
}

esp_err_t api_regler_post(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  std::string fejl;
  auto j = laes_json(req, fejl);
  if (!j) return send_fejl(req, "400 Bad Request", fejl);
  std::vector<ihc::Regel> regler;
  const cJSON *o;
  cJSON_ArrayForEach(o, cJSON_GetObjectItemCaseSensitive(j.get(), "regler")) {
    int im = 0, ik = 0, um = 0, uk = 0;
    if (json_str(o, "type", "kip") != "kip" || !json_int(o, "ind_modul", im) || !json_int(o, "ind_kanal", ik) ||
        !json_int(o, "ud_modul", um) || !json_int(o, "ud_kanal", uk) || im < 1 || ik < 1 || um < 1 || uk < 1) {
      return send_fejl(req, "400 Bad Request", "Ugyldig regel nr. " + std::to_string(regler.size() + 1));
    }
    const ihc::Regel r{ihc::RegelType::kKip, static_cast<uint8_t>(im - 1), static_cast<uint8_t>(ik - 1),
                       static_cast<uint8_t>(um - 1), static_cast<uint8_t>(uk - 1)};
    if (!ihc::regel_gyldig(r)) {
      return send_fejl(req, "400 Bad Request", "Ugyldig regel nr. " + std::to_string(regler.size() + 1));
    }
    regler.push_back(r);
  }
  if (regler.size() > ihc::Regeltabel::kMaxRegler) return send_fejl(req, "400 Bad Request", "For mange regler");
  if (!tilstand::gem_regler(regler.data(), regler.size())) {
    return send_fejl(req, "500 Internal Server Error", "Kunne ikke gemme reglerne");
  }
  return send_ok(req);
}

// ---------------------------------------------------------------------------
// Wi-Fi og MQTT
// ---------------------------------------------------------------------------

std::string ip_tekst(uint32_t a) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", (unsigned)(a & 0xFF), (unsigned)((a >> 8) & 0xFF),
           (unsigned)((a >> 16) & 0xFF), (unsigned)(a >> 24));
  return a ? buf : "";
}

bool ip_fra_tekst(const std::string &s, uint32_t &ud) {
  if (s.empty()) {
    ud = 0;
    return true;
  }
  unsigned a, b, c, d;
  char rest;
  if (sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &rest) != 4 || a > 255 || b > 255 || c > 255 || d > 255) {
    return false;
  }
  ud = a | (b << 8) | (c << 16) | (d << 24);
  return true;
}

esp_err_t api_wifi_get(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  wifi::Konfig k;
  wifi::hent_konfig(k, true);
  auto j = ny_json();
  cJSON_AddStringToObject(j.get(), "ssid", k.ssid);
  cJSON_AddStringToObject(j.get(), "vaertsnavn", k.vaertsnavn);
  cJSON_AddBoolToObject(j.get(), "fast_ip", k.fast_ip);
  cJSON_AddStringToObject(j.get(), "ip", ip_tekst(k.ip).c_str());
  cJSON_AddStringToObject(j.get(), "maske", ip_tekst(k.maske).c_str());
  cJSON_AddStringToObject(j.get(), "gateway", ip_tekst(k.gateway).c_str());
  cJSON_AddStringToObject(j.get(), "dns", ip_tekst(k.dns).c_str());
  return send_json(req, "200 OK", j.get());
}

esp_err_t api_wifi_post(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  std::string fejl;
  auto j = laes_json(req, fejl);
  if (!j) return send_fejl(req, "400 Bad Request", fejl);
  wifi::Konfig k;
  wifi::hent_konfig(k, false);  // behold koden, hvis der ikke sendes en ny
  const std::string ssid = json_str(j.get(), "ssid", k.ssid);
  const std::string vaert = json_str(j.get(), "vaertsnavn", k.vaertsnavn);
  if (ssid.empty() || ssid.size() > 32 || vaert.size() > 32) return send_fejl(req, "400 Bad Request", "Ugyldigt netværk");
  strncpy(k.ssid, ssid.c_str(), sizeof(k.ssid) - 1);
  k.ssid[ssid.size()] = '\0';
  strncpy(k.vaertsnavn, vaert.c_str(), sizeof(k.vaertsnavn) - 1);
  k.vaertsnavn[vaert.size()] = '\0';
  const cJSON *kode = cJSON_GetObjectItemCaseSensitive(j.get(), "kode");
  if (cJSON_IsString(kode) && kode->valuestring[0]) {
    if (strlen(kode->valuestring) > 64) return send_fejl(req, "400 Bad Request", "Wi-Fi-koden er for lang");
    memset(k.kode, 0, sizeof(k.kode));
    strncpy(k.kode, kode->valuestring, sizeof(k.kode) - 1);
  }
  k.fast_ip = json_bool(j.get(), "fast_ip", k.fast_ip);
  if (!ip_fra_tekst(json_str(j.get(), "ip", ip_tekst(k.ip)), k.ip) ||
      !ip_fra_tekst(json_str(j.get(), "maske", ip_tekst(k.maske)), k.maske) ||
      !ip_fra_tekst(json_str(j.get(), "gateway", ip_tekst(k.gateway)), k.gateway) ||
      !ip_fra_tekst(json_str(j.get(), "dns", ip_tekst(k.dns)), k.dns)) {
    return send_fejl(req, "400 Bad Request", "Ugyldig IP-adresse");
  }
  if (k.fast_ip && (!k.ip || !k.maske || !k.gateway)) {
    return send_fejl(req, "400 Bad Request", "Fast IP kræver IP-adresse, maske og gateway");
  }
  return wifi::gem_konfig(k) ? send_ok(req) : send_fejl(req, "500 Internal Server Error", "Kunne ikke gemme");
}

esp_err_t api_mqtt_get(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  const mqtt::Konfig k = mqtt::hent_konfig(true);
  auto j = ny_json();
  cJSON_AddStringToObject(j.get(), "vaert", k.vaert.c_str());
  cJSON_AddNumberToObject(j.get(), "port", k.port);
  cJSON_AddStringToObject(j.get(), "bruger", k.bruger.c_str());
  cJSON_AddBoolToObject(j.get(), "har_kode", !mqtt::hent_konfig(false).kode.empty());
  cJSON_AddStringToObject(j.get(), "klient_id", k.klient_id.c_str());
  cJSON_AddStringToObject(j.get(), "basis_topic", k.basis_topic.c_str());
  cJSON_AddStringToObject(j.get(), "payload_on", k.payload_on.c_str());
  cJSON_AddStringToObject(j.get(), "payload_off", k.payload_off.c_str());
  cJSON_AddBoolToObject(j.get(), "retain", k.retain);
  cJSON_AddNumberToObject(j.get(), "qos", k.qos);
  cJSON_AddNumberToObject(j.get(), "keepalive", k.keepalive);
  cJSON_AddBoolToObject(j.get(), "discovery", k.discovery);
  cJSON_AddStringToObject(j.get(), "discovery_prefix", k.discovery_prefix.c_str());
  return send_json(req, "200 OK", j.get());
}

esp_err_t api_mqtt_post(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  std::string fejl;
  auto j = laes_json(req, fejl);
  if (!j) return send_fejl(req, "400 Bad Request", fejl);
  mqtt::Konfig k = mqtt::hent_konfig(false);
  k.vaert = json_str(j.get(), "vaert", k.vaert);
  k.bruger = json_str(j.get(), "bruger", k.bruger);
  k.klient_id = json_str(j.get(), "klient_id", k.klient_id);
  k.basis_topic = json_str(j.get(), "basis_topic", k.basis_topic);
  k.payload_on = json_str(j.get(), "payload_on", k.payload_on);
  k.payload_off = json_str(j.get(), "payload_off", k.payload_off);
  k.discovery_prefix = json_str(j.get(), "discovery_prefix", k.discovery_prefix);
  k.retain = json_bool(j.get(), "retain", k.retain);
  k.discovery = json_bool(j.get(), "discovery", k.discovery);
  int v = 0;
  if (json_int(j.get(), "port", v)) k.port = static_cast<uint16_t>(v > 0 && v < 65536 ? v : 0);
  if (json_int(j.get(), "qos", v)) k.qos = static_cast<uint8_t>(v ? 1 : 0);
  if (json_int(j.get(), "keepalive", v)) k.keepalive = static_cast<uint16_t>(v < 5 ? 5 : (v > 3600 ? 3600 : v));
  // Koden ændres kun, hvis der sendes en ny (UI'et viser aldrig den gamle).
  const cJSON *kode = cJSON_GetObjectItemCaseSensitive(j.get(), "kode");
  const bool ny_kode = cJSON_IsString(kode);
  if (ny_kode) k.kode = kode->valuestring;
  if (!mqtt::gem_konfig(k, !ny_kode, fejl)) return send_fejl(req, "400 Bad Request", fejl);
  return send_ok(req);
}

// ---------------------------------------------------------------------------
// Eksport og import af konfiguration
// ---------------------------------------------------------------------------
//
// Indeholder aktive moduler, navne og typer, regler og MQTT (uden kode).
// Wi-Fi er ikke med: det hører til den enkelte enhed og kunne afbryde
// forbindelsen ved import.

constexpr const char *kEksportFormat = "ihc-bridge-konfig";

esp_err_t api_eksport(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  auto j = ny_json();
  cJSON_AddStringToObject(j.get(), "format", kEksportFormat);
  cJSON_AddNumberToObject(j.get(), "version", 1);
  cJSON_AddStringToObject(j.get(), "firmware", IHC_VERSION);

  cJSON *mod = cJSON_AddObjectToObject(j.get(), "moduler");
  cJSON *mu = cJSON_AddArrayToObject(mod, "udgange");
  cJSON *mi = cJSON_AddArrayToObject(mod, "indgange");
  for (uint32_t m = 1; m <= ihc::kOutputModules; ++m) {
    if (opsaetning::aktive_outputmoduler() & (1u << (m - 1))) cJSON_AddItemToArray(mu, cJSON_CreateNumber(m));
  }
  for (uint32_t m = 1; m <= ihc::kInputModules; ++m) {
    if (opsaetning::aktive_inputmoduler() & (1u << (m - 1))) cJSON_AddItemToArray(mi, cJSON_CreateNumber(m));
  }

  // Kun kanaler med eget navn eller anden type end standard.
  cJSON *ud = cJSON_AddArrayToObject(j.get(), "udgange");
  for (uint32_t m = 1; m <= ihc::kOutputModules; ++m) {
    for (uint32_t k = 1; k <= ihc::kOutputsPerModule; ++k) {
      const auto t = opsaetning::udgangstype(m, k);
      if (!opsaetning::har_eget_udgangsnavn(m, k) && t == netvaerk::UdgangsType::kLys) continue;
      cJSON *o = cJSON_CreateObject();
      cJSON_AddNumberToObject(o, "modul", m);
      cJSON_AddNumberToObject(o, "kanal", k);
      cJSON_AddStringToObject(o, "navn", opsaetning::har_eget_udgangsnavn(m, k) ? opsaetning::udgangsnavn(m, k).c_str() : "");
      cJSON_AddStringToObject(o, "type", netvaerk::udgangstype_navn(t));
      cJSON_AddItemToArray(ud, o);
    }
  }
  cJSON *ind = cJSON_AddArrayToObject(j.get(), "indgange");
  for (uint32_t m = 1; m <= ihc::kInputModules; ++m) {
    for (uint32_t k = 1; k <= ihc::kInputsPerModule; ++k) {
      const auto t = opsaetning::indgangstype(m, k);
      if (!opsaetning::har_eget_indgangsnavn(m, k) && t == netvaerk::IndgangsType::kTryk) continue;
      cJSON *o = cJSON_CreateObject();
      cJSON_AddNumberToObject(o, "modul", m);
      cJSON_AddNumberToObject(o, "kanal", k);
      cJSON_AddStringToObject(o, "navn", opsaetning::har_eget_indgangsnavn(m, k) ? opsaetning::indgangsnavn(m, k).c_str() : "");
      cJSON_AddStringToObject(o, "type", netvaerk::indgangstype_navn(t));
      cJSON_AddItemToArray(ind, o);
    }
  }

  std::vector<ihc::Regel> regler(ihc::Regeltabel::kMaxRegler);
  const size_t n = ihc_bus::regler(regler.data(), regler.size());
  cJSON *ra = cJSON_AddArrayToObject(j.get(), "regler");
  for (size_t i = 0; i < n; ++i) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "kip");
    cJSON_AddNumberToObject(o, "ind_modul", regler[i].in_module + 1);
    cJSON_AddNumberToObject(o, "ind_kanal", regler[i].in_channel + 1);
    cJSON_AddNumberToObject(o, "ud_modul", regler[i].out_module + 1);
    cJSON_AddNumberToObject(o, "ud_kanal", regler[i].out_channel + 1);
    cJSON_AddItemToArray(ra, o);
  }

  const mqtt::Konfig mk = mqtt::hent_konfig(true);
  cJSON *mq = cJSON_AddObjectToObject(j.get(), "mqtt");
  cJSON_AddStringToObject(mq, "vaert", mk.vaert.c_str());
  cJSON_AddNumberToObject(mq, "port", mk.port);
  cJSON_AddStringToObject(mq, "bruger", mk.bruger.c_str());
  cJSON_AddStringToObject(mq, "klient_id", mk.klient_id.c_str());
  cJSON_AddStringToObject(mq, "basis_topic", mk.basis_topic.c_str());
  cJSON_AddStringToObject(mq, "payload_on", mk.payload_on.c_str());
  cJSON_AddStringToObject(mq, "payload_off", mk.payload_off.c_str());
  cJSON_AddBoolToObject(mq, "retain", mk.retain);
  cJSON_AddNumberToObject(mq, "qos", mk.qos);
  cJSON_AddNumberToObject(mq, "keepalive", mk.keepalive);
  cJSON_AddBoolToObject(mq, "discovery", mk.discovery);
  cJSON_AddStringToObject(mq, "discovery_prefix", mk.discovery_prefix.c_str());

  httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"ihc-bridge-konfig.json\"");
  return send_json(req, "200 OK", j.get());
}

esp_err_t api_import(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  std::string fejl;
  auto j = laes_json(req, fejl);
  if (!j) return send_fejl(req, "400 Bad Request", fejl);
  if (json_str(j.get(), "format") != kEksportFormat) {
    return send_fejl(req, "400 Bad Request", "Filen er ikke en IHC Bridge-konfiguration");
  }

  // Først kontrolleres alt; der ændres intet, hvis noget er ugyldigt.
  struct Kanal {
    bool udgang;
    int m, k;
    std::string navn;
    uint8_t type;
  };
  std::vector<Kanal> kanaler;
  for (const char *felt : {"udgange", "indgange"}) {
    const bool udgang = strcmp(felt, "udgange") == 0;
    const cJSON *o;
    cJSON_ArrayForEach(o, cJSON_GetObjectItemCaseSensitive(j.get(), felt)) {
      Kanal kan{udgang, 0, 0, json_str(o, "navn"), 0};
      const std::string type = json_str(o, "type");
      bool ok = json_int(o, "modul", kan.m) && json_int(o, "kanal", kan.k) && kan.navn.size() <= opsaetning::kMaxNavn;
      if (udgang) {
        netvaerk::UdgangsType t;
        ok = ok && kan.m >= 1 && kan.m <= (int)ihc::kOutputModules && kan.k >= 1 &&
             kan.k <= (int)ihc::kOutputsPerModule && netvaerk::udgangstype_fra_navn(type, t);
        kan.type = static_cast<uint8_t>(t);
      } else {
        netvaerk::IndgangsType t;
        ok = ok && kan.m >= 1 && kan.m <= (int)ihc::kInputModules && kan.k >= 1 &&
             kan.k <= (int)ihc::kInputsPerModule && netvaerk::indgangstype_fra_navn(type, t);
        kan.type = static_cast<uint8_t>(t);
      }
      if (!ok) return send_fejl(req, "400 Bad Request", std::string("Ugyldig kanal under ") + felt);
      kanaler.push_back(kan);
    }
  }
  std::vector<ihc::Regel> regler;
  const cJSON *o;
  cJSON_ArrayForEach(o, cJSON_GetObjectItemCaseSensitive(j.get(), "regler")) {
    int im = 0, ik = 0, um = 0, uk = 0;
    json_int(o, "ind_modul", im);
    json_int(o, "ind_kanal", ik);
    json_int(o, "ud_modul", um);
    json_int(o, "ud_kanal", uk);
    const ihc::Regel r{ihc::RegelType::kKip, static_cast<uint8_t>(im - 1), static_cast<uint8_t>(ik - 1),
                       static_cast<uint8_t>(um - 1), static_cast<uint8_t>(uk - 1)};
    if (im < 1 || ik < 1 || um < 1 || uk < 1 || !ihc::regel_gyldig(r)) {
      return send_fejl(req, "400 Bad Request", "Ugyldig regel nr. " + std::to_string(regler.size() + 1));
    }
    regler.push_back(r);
  }
  if (regler.size() > ihc::Regeltabel::kMaxRegler) return send_fejl(req, "400 Bad Request", "For mange regler");

  // Moduler.
  uint16_t out = 0;
  uint8_t in = 0;
  const cJSON *mod = cJSON_GetObjectItemCaseSensitive(j.get(), "moduler");
  const cJSON *v;
  cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(mod, "udgange")) {
    if (cJSON_IsNumber(v) && v->valueint >= 1 && v->valueint <= (int)ihc::kOutputModules) out |= 1u << (v->valueint - 1);
  }
  cJSON_ArrayForEach(v, cJSON_GetObjectItemCaseSensitive(mod, "indgange")) {
    if (cJSON_IsNumber(v) && v->valueint >= 1 && v->valueint <= (int)ihc::kInputModules) in |= 1u << (v->valueint - 1);
  }

  // MQTT (koden i enheden bevares).
  const cJSON *mq = cJSON_GetObjectItemCaseSensitive(j.get(), "mqtt");
  if (cJSON_IsObject(mq)) {
    mqtt::Konfig k = mqtt::hent_konfig(false);
    k.vaert = json_str(mq, "vaert", k.vaert);
    k.bruger = json_str(mq, "bruger", k.bruger);
    k.klient_id = json_str(mq, "klient_id", k.klient_id);
    k.basis_topic = json_str(mq, "basis_topic", k.basis_topic);
    k.payload_on = json_str(mq, "payload_on", k.payload_on);
    k.payload_off = json_str(mq, "payload_off", k.payload_off);
    k.discovery_prefix = json_str(mq, "discovery_prefix", k.discovery_prefix);
    k.retain = json_bool(mq, "retain", k.retain);
    k.discovery = json_bool(mq, "discovery", k.discovery);
    int n = 0;
    if (json_int(mq, "port", n) && n > 0 && n < 65536) k.port = static_cast<uint16_t>(n);
    if (json_int(mq, "qos", n)) k.qos = n ? 1 : 0;
    if (json_int(mq, "keepalive", n) && n >= 5 && n <= 3600) k.keepalive = static_cast<uint16_t>(n);
    if (!mqtt::gem_konfig(k, true, fejl)) return send_fejl(req, "400 Bad Request", "MQTT: " + fejl);
  }

  // Alle kanaler nulstilles til standard, derefter sættes de importerede.
  for (uint32_t m = 1; m <= ihc::kOutputModules; ++m) {
    for (uint32_t k = 1; k <= ihc::kOutputsPerModule; ++k) {
      if (opsaetning::har_eget_udgangsnavn(m, k) || opsaetning::udgangstype(m, k) != netvaerk::UdgangsType::kLys) {
        opsaetning::gem_udgang(m, k, "", netvaerk::UdgangsType::kLys);
      }
    }
  }
  for (uint32_t m = 1; m <= ihc::kInputModules; ++m) {
    for (uint32_t k = 1; k <= ihc::kInputsPerModule; ++k) {
      if (opsaetning::har_eget_indgangsnavn(m, k) || opsaetning::indgangstype(m, k) != netvaerk::IndgangsType::kTryk) {
        opsaetning::gem_indgang(m, k, "", netvaerk::IndgangsType::kTryk);
      }
    }
  }
  bool ok = true;
  for (const auto &kan : kanaler) {
    ok &= kan.udgang ? opsaetning::gem_udgang(kan.m, kan.k, kan.navn, static_cast<netvaerk::UdgangsType>(kan.type))
                     : opsaetning::gem_indgang(kan.m, kan.k, kan.navn, static_cast<netvaerk::IndgangsType>(kan.type));
  }
  ok &= opsaetning::gem_aktive_moduler(out, in);
  ok &= tilstand::gem_regler(regler.data(), regler.size());
  if (!ok) return send_fejl(req, "500 Internal Server Error", "Dele af konfigurationen kunne ikke gemmes");
  ESP_LOGI(TAG, "Konfiguration importeret: %u kanaler, %u regler", (unsigned)kanaler.size(), (unsigned)regler.size());
  return send_ok(req);
}

// ---------------------------------------------------------------------------
// Systemlog
// ---------------------------------------------------------------------------

esp_err_t api_log(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  uint32_t efter = 0;
  char q[32] = {}, v[12] = {};
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
      httpd_query_key_value(q, "efter", v, sizeof(v)) == ESP_OK) {
    efter = static_cast<uint32_t>(strtoul(v, nullptr, 10));
  }
  auto j = ny_json();
  cJSON *arr = cJSON_AddArrayToObject(j.get(), "linjer");
  const uint32_t sidste = logbog::hent(efter, [&](uint32_t nr, const char *tekst) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "nr", nr);
    cJSON_AddStringToObject(o, "tekst", tekst);
    cJSON_AddItemToArray(arr, o);
  });
  cJSON_AddNumberToObject(j.get(), "sidste", sidste);
  return send_json(req, "200 OK", j.get());
}

// ---------------------------------------------------------------------------
// Firmware og genstart
// ---------------------------------------------------------------------------

void genstart_om(uint32_t ms) {
  static esp_timer_handle_t t = nullptr;
  if (!t) {
    esp_timer_create_args_t a = {};
    a.callback = [](void *) { esp_restart(); };
    a.name = "genstart";
    esp_timer_create(&a, &t);
  }
  esp_timer_start_once(t, static_cast<uint64_t>(ms) * 1000);
}

esp_err_t api_genstart(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  ESP_LOGW(TAG, "Genstart bestilt fra web-UI");
  genstart_om(1000);
  return send_ok(req);
}

esp_err_t api_firmware(httpd_req_t *req) {
  if (!kraev_login(req)) return ESP_OK;
  const esp_partition_t *ny = esp_ota_get_next_update_partition(nullptr);
  if (!ny) return send_fejl(req, "500 Internal Server Error", "Ingen OTA-partition");
  if (req->content_len < 1024 || req->content_len > ny->size) {
    return send_fejl(req, "400 Bad Request", "Ugyldig filstørrelse");
  }

  std::unique_ptr<char[]> buf(new char[4096]);
  esp_ota_handle_t ota = 0;
  bool startet = false;
  size_t laest = 0;
  std::string fejl;

  while (laest < req->content_len) {
    const int r = httpd_req_recv(req, buf.get(), 4096);
    if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
    if (r <= 0) {
      fejl = "Overførslen blev afbrudt";
      break;
    }
    if (!startet) {
      // Tjek at filen er firmware til netop dette projekt, før der skrives.
      constexpr size_t kDescOff = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
      if (static_cast<size_t>(r) < kDescOff + sizeof(esp_app_desc_t)) {
        fejl = "Filen er ikke en gyldig firmware";
        break;
      }
      const auto *desc = reinterpret_cast<const esp_app_desc_t *>(buf.get() + kDescOff);
      if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD ||
          strncmp(desc->project_name, esp_app_get_description()->project_name, sizeof(desc->project_name)) != 0) {
        fejl = "Filen er ikke firmware til IHC Bridge";
        break;
      }
      ESP_LOGW(TAG, "Firmwareopdatering til version %s startet", desc->version);
      if (esp_ota_begin(ny, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
        fejl = "Kunne ikke starte opdateringen";
        break;
      }
      startet = true;
    }
    if (esp_ota_write(ota, buf.get(), r) != ESP_OK) {
      fejl = "Fejl under skrivning til flash";
      break;
    }
    laest += r;
  }

  if (fejl.empty() && startet) {
    const esp_err_t err = esp_ota_end(ota);
    startet = false;
    if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
      fejl = "Firmwaren er beskadiget (kontrolsum passer ikke)";
    } else if (err != ESP_OK || esp_ota_set_boot_partition(ny) != ESP_OK) {
      fejl = "Kunne ikke aktivere den nye firmware";
    }
  }
  if (startet) esp_ota_abort(ota);
  if (!fejl.empty()) {
    ESP_LOGE(TAG, "Firmwareopdatering fejlede: %s", fejl.c_str());
    return send_fejl(req, "400 Bad Request", fejl);
  }
  ESP_LOGW(TAG, "Ny firmware er klar - genstarter");
  genstart_om(1500);
  return send_ok(req);
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------

struct WsBesked {
  std::string tekst;
};

void ws_send_alle_work(void *arg) {
  std::unique_ptr<WsBesked> b(static_cast<WsBesked *>(arg));
  size_t n = 8;
  int fds[8];
  if (httpd_get_client_list(s_server, &n, fds) != ESP_OK) return;
  httpd_ws_frame_t f = {};
  f.type = HTTPD_WS_TYPE_TEXT;
  f.payload = reinterpret_cast<uint8_t *>(b->tekst.data());
  f.len = b->tekst.size();
  for (size_t i = 0; i < n; ++i) {
    if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
      httpd_ws_send_frame_async(s_server, fds[i], &f);
    }
  }
}

void ws_send_alle(const std::string &tekst) {
  if (!s_server) return;
  auto *b = new WsBesked{tekst};
  if (httpd_queue_work(s_server, ws_send_alle_work, b) != ESP_OK) delete b;
}

bool har_ws_klienter() {
  if (!s_server) return false;
  size_t n = 8;
  int fds[8];
  if (httpd_get_client_list(s_server, &n, fds) != ESP_OK) return false;
  for (size_t i = 0; i < n; ++i) {
    if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) return true;
  }
  return false;
}

esp_err_t ws_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    // Håndtryk: kun med gyldig session.
    if (!sikkerhed::har_kode() || !sikkerhed::session_gyldig(laes_cookie(req))) {
      ESP_LOGW(TAG, "WebSocket afvist: ikke logget ind");
      return ESP_FAIL;
    }
    s_ny_klient = true;  // send fuld status til den nye klient
    xTaskNotifyGive(s_push_task);
    return ESP_OK;
  }
  httpd_ws_frame_t f = {};
  f.type = HTTPD_WS_TYPE_TEXT;
  if (httpd_ws_recv_frame(req, &f, 0) != ESP_OK || f.len == 0 || f.len > 512) return ESP_OK;
  std::string data(f.len, '\0');
  f.payload = reinterpret_cast<uint8_t *>(data.data());
  if (httpd_ws_recv_frame(req, &f, f.len) != ESP_OK) return ESP_OK;
  if (f.type != HTTPD_WS_TYPE_TEXT) return ESP_OK;

  Json j(cJSON_ParseWithLength(data.data(), data.size()), cJSON_Delete);
  if (!j) return ESP_OK;
  if (json_str(j.get(), "type") == "udgang") {
    std::string fejl;
    if (!udfoer_udgang(j.get(), fejl)) ESP_LOGW(TAG, "WebSocket-kommando afvist: %s", fejl.c_str());
  }
  return ESP_OK;
}

// Sender ændringer i udgange og indgange live til alle åbne browsere.
void push_task(void *) {
  uint8_t sendt_ud[ihc::kOutputModules] = {};
  uint16_t sendt_ind[ihc::kInputModules] = {};
  int8_t sendt_online[ihc::kInputModules] = {};
  bool alt = true;
  uint32_t temp_version = 0;
  auto &rx = ihc_bus::modtager();

  for (;;) {
    // En ændring eller en ny klient vækker tasken straks; ellers hvert 200 ms
    // (fanger inputmoduler, der går online/offline).
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kPushPollMs));
    if (s_ny_klient.exchange(false)) alt = true;
    if (!har_ws_klienter()) {
      alt = true;  // næste klient får alt
      continue;
    }
    auto j = ny_json();
    cJSON *ud = nullptr;
    cJSON *ind = nullptr;
    for (uint32_t m = 0; m < ihc::kOutputModules; ++m) {
      const uint8_t v = ihc_bus::output_module(m);
      if (!alt && v == sendt_ud[m]) continue;
      if (!ud) ud = cJSON_AddObjectToObject(j.get(), "udgange");
      cJSON_AddNumberToObject(ud, std::to_string(m + 1).c_str(), v);
      sendt_ud[m] = v;
    }
    for (uint32_t m = 0; m < ihc::kInputModules; ++m) {
      const bool online = rx.online(m);
      const uint16_t v = rx.state(m);
      if (!alt && v == sendt_ind[m] && sendt_online[m] == (online ? 1 : 0)) continue;
      if (!ind) ind = cJSON_AddObjectToObject(j.get(), "indgange");
      cJSON *o = cJSON_AddObjectToObject(ind, std::to_string(m + 1).c_str());
      cJSON_AddBoolToObject(o, "online", online);
      cJSON_AddNumberToObject(o, "tilstand", v);
      sendt_ind[m] = v;
      sendt_online[m] = online ? 1 : 0;
    }
    // Temperaturer sendes samlet, når der er nye målinger.
    const uint32_t tv = temperatur::version();
    if (alt || tv != temp_version) {
      temp_version = tv;
      cJSON *temp = nullptr;
      for (uint32_t m = 1; m <= ihc::kInputModules; ++m) {
        for (uint32_t k = 1; k <= ihc::kInputsPerModule; ++k) {
          if (!temperatur::er_foeler(m, k)) continue;
          if (!temp) temp = cJSON_AddObjectToObject(j.get(), "temperaturer");
          cJSON_AddItemToObject(temp, (std::to_string(m) + "." + std::to_string(k)).c_str(), temperatur_json(m, k));
        }
      }
      if (temp && !ud && !ind) ud = temp;  // så beskeden sendes
    }
    alt = false;
    if (!ud && !ind) continue;
    cJSON_AddStringToObject(j.get(), "type", "tilstand");
    char *tekst = cJSON_PrintUnformatted(j.get());
    if (tekst) {
      ws_send_alle(tekst);
      cJSON_free(tekst);
    }
  }
}

// ---------------------------------------------------------------------------
// Opsætning af server
// ---------------------------------------------------------------------------

struct Rute {
  const char *uri;
  httpd_method_t metode;
  esp_err_t (*handler)(httpd_req_t *);
  bool websocket;
};

const Rute kRuter[] = {
    {"/", HTTP_GET, side_ui, false},
    {"/opsaetning", HTTP_GET, side_opsaetning, false},
    {"/api/opsaetning", HTTP_GET, api_opsaetning_get, false},
    {"/api/opsaetning", HTTP_POST, api_opsaetning_post, false},
    {"/api/wifi/scan", HTTP_GET, api_wifi_scan, false},
    {"/api/login", HTTP_POST, api_login, false},
    {"/api/logud", HTTP_POST, api_logud, false},
    {"/api/kode", HTTP_POST, api_kode, false},
    {"/api/status", HTTP_GET, api_status, false},
    {"/api/io", HTTP_GET, api_io, false},
    {"/api/udgang", HTTP_POST, api_udgang, false},
    {"/api/kanal", HTTP_POST, api_kanal, false},
    {"/api/moduler", HTTP_POST, api_moduler, false},
    {"/api/regler", HTTP_GET, api_regler_get, false},
    {"/api/regler", HTTP_POST, api_regler_post, false},
    {"/api/wifi", HTTP_GET, api_wifi_get, false},
    {"/api/wifi", HTTP_POST, api_wifi_post, false},
    {"/api/mqtt", HTTP_GET, api_mqtt_get, false},
    {"/api/mqtt", HTTP_POST, api_mqtt_post, false},
    {"/api/firmware", HTTP_POST, api_firmware, false},
    {"/api/genstart", HTTP_POST, api_genstart, false},
    {"/api/eksport", HTTP_GET, api_eksport, false},
    {"/api/import", HTTP_POST, api_import, false},
    {"/api/log", HTTP_GET, api_log, false},
    {"/ws", HTTP_GET, ws_handler, true},
};

esp_err_t ikke_fundet(httpd_req_t *req, httpd_err_code_t) {
  // Før opsætning sendes alt til opsætningssiden (hjælper telefoner, der
  // åbner en tilfældig adresse, når de forbinder til opsætnings-AP'et).
  if (!sikkerhed::har_kode() && strncmp(req->uri, "/api/", 5) != 0) return omdiriger(req, "/");
  return send_fejl(req, "404 Not Found", "Ikke fundet");
}

}  // namespace

void start() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.core_id = 0;
  cfg.stack_size = 8192;
  cfg.max_uri_handlers = sizeof(kRuter) / sizeof(kRuter[0]) + 2;
  cfg.max_open_sockets = 7;
  cfg.lru_purge_enable = true;
  cfg.recv_wait_timeout = 10;
  cfg.send_wait_timeout = 10;
  if (httpd_start(&s_server, &cfg) != ESP_OK) {
    ESP_LOGE(TAG, "Webserveren kunne ikke starte");
    return;
  }
  for (const auto &r : kRuter) {
    httpd_uri_t u = {};
    u.uri = r.uri;
    u.method = r.metode;
    u.handler = r.handler;
    u.is_websocket = r.websocket;
    httpd_register_uri_handler(s_server, &u);
  }
  httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, ikke_fundet);
  xTaskCreatePinnedToCore(push_task, "web_push", 4096, nullptr, 4, &s_push_task, 0);
  ESP_LOGI(TAG, "Webserver startet på port 80");
}

void luk_websockets() {
  if (!s_server) return;
  size_t n = 8;
  int fds[8];
  if (httpd_get_client_list(s_server, &n, fds) != ESP_OK) return;
  for (size_t i = 0; i < n; ++i) {
    if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) httpd_sess_trigger_close(s_server, fds[i]);
  }
}

void aendring() {
  if (s_push_task) xTaskNotifyGive(s_push_task);
}

}  // namespace web
