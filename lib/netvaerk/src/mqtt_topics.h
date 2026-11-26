// MQTT-topics, kommandoer og Home Assistant-discovery.
//
// Ren logik uden ESP-IDF, så den kan testes på PC. Moduler og kanaler
// tælles fra 1 i topics, ligesom på IHC-modulerne:
//
//   <basis>/status                       online / offline (retained, last will)
//   <basis>/output/<modul>/<kanal>/state ON / OFF (retained)
//   <basis>/output/<modul>/<kanal>/set   ON / OFF / TOGGLE (kommando)
//   <basis>/input/<modul>/<kanal>/state  ON / OFF (retained)
//   <basis>/input/<modul>/status         online / offline (retained)
//
// Eksempel: inputmodul 3, indgang 15 -> ihc/ihc-bridge/input/3/15/state
#pragma once

#include <cstdint>
#include <string>

#include "kanaltyper.h"

namespace netvaerk {

constexpr const char *kOnline = "online";
constexpr const char *kOffline = "offline";

std::string topic_status(const std::string &basis);
std::string topic_udgang(const std::string &basis, uint32_t modul, uint32_t kanal);  // .../state
std::string topic_udgang_set(const std::string &basis, uint32_t modul, uint32_t kanal);
std::string topic_udgang_set_alle(const std::string &basis);  // abonnement med wildcards
std::string topic_indgang(const std::string &basis, uint32_t modul, uint32_t kanal);
std::string topic_indgangsmodul_status(const std::string &basis, uint32_t modul);

// Genkender <basis>/output/<modul>/<kanal>/set. modul og kanal returneres fra 1.
bool parse_udgang_set(const std::string &basis, const std::string &topic, uint32_t &modul, uint32_t &kanal);

enum class Kommando : uint8_t { kUgyldig, kTaend, kSluk, kSkift };
// Tolker en kommando. Store/små bogstaver er ligegyldige. Ud over de
// konfigurerede payloads forstås altid ON/OFF/TOGGLE, 1/0 og true/false.
Kommando parse_kommando(const std::string &payload, const std::string &payload_on,
                        const std::string &payload_off);

// Basis-topic må ikke være tom, indeholde wildcards eller ende/starte med '/'.
bool basis_topic_gyldig(const std::string &basis);

// --- Home Assistant-discovery ---

struct Enhed {
  std::string id;        // unikt, f.eks. "ihcbridge_a1b2c3"
  std::string navn;      // vises i HA, f.eks. "IHC Bridge"
  std::string version;   // firmwareversion
  std::string url;       // web-UI, kan være tom
};

// komponent: "light", "switch" eller "fan" (se udgang_komponent()).
std::string discovery_topic_udgang(const std::string &prefix, const Enhed &e, const char *komponent, uint32_t modul,
                                   uint32_t kanal);
std::string discovery_topic_indgang(const std::string &prefix, const Enhed &e, uint32_t modul, uint32_t kanal);

std::string discovery_udgang(const std::string &basis, const Enhed &e, uint32_t modul, uint32_t kanal,
                             const std::string &navn, UdgangsType type, const std::string &payload_on,
                             const std::string &payload_off);
std::string discovery_indgang(const std::string &basis, const Enhed &e, uint32_t modul, uint32_t kanal,
                              const std::string &navn, IndgangsType type, const std::string &payload_on,
                              const std::string &payload_off);

// Escaper en streng til brug i JSON (uden anførselstegn omkring).
std::string json_escape(const std::string &s);

}  // namespace netvaerk
