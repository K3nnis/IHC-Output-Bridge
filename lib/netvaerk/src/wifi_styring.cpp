#include "wifi_styring.h"

namespace netvaerk {

void WifiStyring::start(int64_t nu, bool har_netvaerk, bool har_noed_kode) {
  har_noed_kode_ = har_noed_kode;
  i_opsaetning_ = false;
  opsaetning_slut_ = -1;
  uden_forbindelse_siden_ = nu;
  forsoeg_ = 0;
  if (har_netvaerk) {
    fase_ = Fase::kVenter;
    naeste_forsoeg_ = nu;  // forsøg med det samme
  } else {
    fase_ = Fase::kIntetNetvaerk;
  }
}

void WifiStyring::netvaerk_gemt(int64_t nu) {
  i_opsaetning_ = true;
  opsaetning_slut_ = -1;
  forsoeg_ = 0;
  if (fase_ != Fase::kForbundet) uden_forbindelse_siden_ = nu;
  // Et eventuelt igangværende forsøg afbrydes af driveren; start forfra.
  fase_ = Fase::kVenter;
  naeste_forsoeg_ = nu;
}

void WifiStyring::forbundet(int64_t nu) {
  if (fase_ == Fase::kIntetNetvaerk) return;
  fase_ = Fase::kForbundet;
  forsoeg_ = 0;
  if (i_opsaetning_) {
    i_opsaetning_ = false;
    opsaetning_slut_ = nu + t_.opsaetning_luk_ms;
  }
}

void WifiStyring::afbrudt(int64_t nu) {
  if (fase_ == Fase::kIntetNetvaerk) return;
  if (fase_ == Fase::kForbundet) {
    // Forbindelsen er netop tabt: prøv igen med det samme.
    uden_forbindelse_siden_ = nu;
    opsaetning_slut_ = -1;
    forsoeg_ = 0;
    fase_ = Fase::kVenter;
    naeste_forsoeg_ = nu;
    return;
  }
  if (fase_ == Fase::kForbinder) planlaeg_naeste(nu);
}

void WifiStyring::planlaeg_naeste(int64_t nu) {
  // forsoeg_ er antal forsøg siden tab (1 efter første forsøg).
  const uint32_t i = forsoeg_ - 1;
  const int64_t pause = i < 4 ? t_.hurtige_ms[i] : t_.langsom_ms;
  fase_ = Fase::kVenter;
  naeste_forsoeg_ = nu + pause;
}

bool WifiStyring::tick(int64_t nu) {
  if (fase_ == Fase::kForbinder && nu - forsoeg_start_ >= t_.forsoeg_timeout_ms) {
    planlaeg_naeste(nu);  // intet svar fra driveren: regn det som fejlet
  }
  if (fase_ == Fase::kVenter && nu >= naeste_forsoeg_) {
    fase_ = Fase::kForbinder;
    forsoeg_start_ = nu;
    ++forsoeg_;
    return true;
  }
  return false;
}

ApTilstand WifiStyring::ap(int64_t nu) const {
  if (fase_ == Fase::kIntetNetvaerk || i_opsaetning_) return ApTilstand::kOpsaetning;
  if (fase_ == Fase::kForbundet) {
    return (opsaetning_slut_ >= 0 && nu < opsaetning_slut_) ? ApTilstand::kOpsaetning : ApTilstand::kSlukket;
  }
  if (har_noed_kode_ && nu - uden_forbindelse_siden_ >= t_.noed_ap_efter_ms) return ApTilstand::kNoedbetjening;
  return ApTilstand::kSlukket;
}

int64_t WifiStyring::ms_til_naeste(int64_t nu) const {
  if (fase_ != Fase::kVenter) return 0;
  return naeste_forsoeg_ > nu ? naeste_forsoeg_ - nu : 0;
}

}  // namespace netvaerk
