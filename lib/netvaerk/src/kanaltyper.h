// Kanaltyper: hvad der sidder på en udgang eller indgang. Bestemmer, hvordan
// kanalen vises i web-UI og i Home Assistant (via discovery).
#pragma once

#include <cstdint>
#include <string>

namespace netvaerk {

enum class UdgangsType : uint8_t {
  kLys = 0,          // HA: light
  kStikkontakt = 1,  // HA: switch, device class "outlet"
  kKontakt = 2,      // HA: switch
  kVentilator = 3,   // HA: fan
};
constexpr uint8_t kAntalUdgangsTyper = 4;

enum class IndgangsType : uint8_t {
  kTryk = 0,        // HA: binary_sensor
  kDoer = 1,        // HA: binary_sensor, device class "door"
  kVindue = 2,      // HA: binary_sensor, device class "window"
  kBevaegelse = 3,  // HA: binary_sensor, device class "motion"
  kTemperatur = 4,  // IHC-temperaturføler; HA: sensor(er) for temperatur, fugt og gulv
};
constexpr uint8_t kAntalIndgangsTyper = 5;

// Navne brugt i API'et ("lys", "stikkontakt", ...).
const char *udgangstype_navn(UdgangsType t);
const char *indgangstype_navn(IndgangsType t);
bool udgangstype_fra_navn(const std::string &navn, UdgangsType &t);
bool indgangstype_fra_navn(const std::string &navn, IndgangsType &t);

// Home Assistant-komponent for en udgangstype.
const char *udgang_komponent(UdgangsType t);
// Alle komponenter, en udgang kan have; bruges til at fjerne den gamle,
// når typen skiftes.
constexpr const char *kUdgangsKomponenter[] = {"light", "switch", "fan"};

// Device class i Home Assistant, eller nullptr hvis ingen.
const char *udgang_device_class(UdgangsType t);
const char *indgang_device_class(IndgangsType t);

}  // namespace netvaerk
