#include "kanaltyper.h"

namespace netvaerk {

namespace {
const char *const kUdgangNavne[kAntalUdgangsTyper] = {"lys", "stikkontakt", "kontakt", "ventilator"};
const char *const kIndgangNavne[kAntalIndgangsTyper] = {"tryk", "doer", "vindue", "bevaegelse"};
}  // namespace

const char *udgangstype_navn(UdgangsType t) {
  const auto i = static_cast<uint8_t>(t);
  return i < kAntalUdgangsTyper ? kUdgangNavne[i] : kUdgangNavne[0];
}

const char *indgangstype_navn(IndgangsType t) {
  const auto i = static_cast<uint8_t>(t);
  return i < kAntalIndgangsTyper ? kIndgangNavne[i] : kIndgangNavne[0];
}

bool udgangstype_fra_navn(const std::string &navn, UdgangsType &t) {
  for (uint8_t i = 0; i < kAntalUdgangsTyper; ++i) {
    if (navn == kUdgangNavne[i]) {
      t = static_cast<UdgangsType>(i);
      return true;
    }
  }
  return false;
}

bool indgangstype_fra_navn(const std::string &navn, IndgangsType &t) {
  for (uint8_t i = 0; i < kAntalIndgangsTyper; ++i) {
    if (navn == kIndgangNavne[i]) {
      t = static_cast<IndgangsType>(i);
      return true;
    }
  }
  return false;
}

const char *udgang_komponent(UdgangsType t) {
  if (t == UdgangsType::kLys) return "light";
  if (t == UdgangsType::kVentilator) return "fan";
  return "switch";
}

const char *udgang_device_class(UdgangsType t) {
  if (t == UdgangsType::kStikkontakt) return "outlet";
  if (t == UdgangsType::kKontakt) return "switch";
  return nullptr;
}

const char *indgang_device_class(IndgangsType t) {
  if (t == IndgangsType::kDoer) return "door";
  if (t == IndgangsType::kVindue) return "window";
  if (t == IndgangsType::kBevaegelse) return "motion";
  return nullptr;
}

}  // namespace netvaerk
