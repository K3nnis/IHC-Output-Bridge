// MQTT: udgange, indgange og status til Home Assistant og andre platforme.
//
// Topics og discovery bygges af lib/netvaerk/mqtt_topics (testet på PC).
// Kommandoer fra MQTT udføres med det samme på bussen; tilstande sendes
// fra en task på kerne 0, når bussen melder en ændring.
#pragma once

#include <cstdint>
#include <string>

namespace mqtt {

struct Konfig {
  std::string vaert;  // IP eller værtsnavn; tom = MQTT slået fra
  uint16_t port = 1883;
  std::string bruger;
  std::string kode;
  std::string klient_id;  // tom = "ihc-bridge-<id>"
  std::string basis_topic = "ihc/ihc-bridge";
  std::string payload_on = "ON";
  std::string payload_off = "OFF";
  bool retain = true;
  uint8_t qos = 0;
  uint16_t keepalive = 30;
  bool discovery = true;
  std::string discovery_prefix = "homeassistant";
};

struct Status {
  bool aktiveret = false;  // broker er sat op
  bool forbundet = false;
  std::string fejl;        // seneste fejl, tom hvis ingen
  uint32_t sendt = 0;
  uint32_t modtaget = 0;
};

// Starter MQTT. Kræver at NVS, opsætning og Wi-Fi er startet.
void start();

Status status();
Konfig hent_konfig(bool skjul_kode = true);
// Validerer, gemmer og genstarter forbindelsen med den nye konfiguration.
// Er kode tom og skjul-markeret i UI'et, beholdes den gamle kode.
bool gem_konfig(const Konfig &k, bool behold_kode, std::string &fejl);

// Kaldes af tilstand, når bussen melder ændringer.
void udgange_aendret(uint16_t moduler);
void indgange_aendret(uint8_t moduler);
// Navne, typer eller aktive moduler er ændret; discovery sendes igen.
void konfiguration_aendret();

}  // namespace mqtt
