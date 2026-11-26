// WifiStyring: beslutter, hvornår der skal forbindes til Wi-Fi, og hvilket
// access point (AP) enheden selv skal tilbyde.
//
// Ren logik uden ESP-IDF, så den kan testes på PC. src/wifi.cpp udfører
// beslutningerne med ESP-IDF's Wi-Fi-driver.
//
// Regler:
//  - Intet netværk gemt:            åbent opsætnings-AP "IHC-Bridge-Opsætning".
//  - Nyt netværk gemt fra opsætning: opsætnings-AP holdes åbent, indtil enheden
//                                    har forbindelse, og lukkes 60 s efter.
//  - Forbindelse mistet:             nye forsøg straks og efter 5, 15, 30 og 60 s,
//                                    derefter hvert 10. minut uden stop.
//  - 10 min uden forbindelse:        nødbetjenings-AP åbnes (kræver adgangskode).
//                                    Det lukkes, så snart forbindelsen er tilbage.
#pragma once

#include <cstdint>

namespace netvaerk {

enum class ApTilstand : uint8_t {
  kSlukket = 0,
  kOpsaetning = 1,     // åbent, til førstegangsopsætning
  kNoedbetjening = 2,  // beskyttet med UI-adgangskoden
};

enum class Fase : uint8_t {
  kIntetNetvaerk = 0,  // ingen Wi-Fi gemt
  kForbinder = 1,      // forsøg i gang
  kVenter = 2,         // venter på næste forsøg
  kForbundet = 3,
};

struct WifiTider {
  // Pause før forsøg nr. 2, 3, 4, 5 efter mistet forbindelse.
  int64_t hurtige_ms[4] = {5000, 15000, 30000, 60000};
  int64_t langsom_ms = 10 * 60 * 1000;          // derefter hvert 10. minut
  int64_t noed_ap_efter_ms = 10 * 60 * 1000;    // nødbetjenings-AP efter 10 min
  int64_t opsaetning_luk_ms = 60 * 1000;        // opsætnings-AP lukkes 60 s efter forbindelse
  int64_t forsoeg_timeout_ms = 30 * 1000;       // et forsøg uden svar regnes som fejlet
};

class WifiStyring {
 public:
  explicit WifiStyring(const WifiTider &tider = WifiTider()) : t_(tider) {}

  // Ved opstart.
  void start(int64_t nu, bool har_netvaerk, bool har_noed_kode);
  // Nyt netværk gemt (fra opsætningssiden).
  void netvaerk_gemt(int64_t nu);
  // UI-adgangskoden er sat eller ændret.
  void noed_kode_sat(bool har_noed_kode) { har_noed_kode_ = har_noed_kode; }

  // Hændelser fra Wi-Fi-driveren.
  void forbundet(int64_t nu);  // har fået IP-adresse
  void afbrudt(int64_t nu);    // forbindelse tabt eller forsøg fejlet

  // Kaldes jævnligt. Returnerer true, hvis et forbindelsesforsøg skal startes nu.
  bool tick(int64_t nu);

  ApTilstand ap(int64_t nu) const;
  Fase fase() const { return fase_; }
  uint32_t forsoeg_siden_tab() const { return forsoeg_; }
  // Tid til næste forsøg (ms), 0 hvis et forsøg er i gang eller forbundet.
  int64_t ms_til_naeste(int64_t nu) const;

 private:
  void planlaeg_naeste(int64_t nu);

  WifiTider t_;
  Fase fase_ = Fase::kIntetNetvaerk;
  bool har_noed_kode_ = false;
  bool i_opsaetning_ = false;      // nyt netværk, endnu ikke forbundet
  int64_t opsaetning_slut_ = -1;   // hvornår opsætnings-AP lukkes efter forbindelse
  int64_t uden_forbindelse_siden_ = 0;
  int64_t naeste_forsoeg_ = 0;
  int64_t forsoeg_start_ = 0;
  uint32_t forsoeg_ = 0;
};

}  // namespace netvaerk
