// IHC-temperaturføler: afkodning af føler-signalet på en indgang.
//
// En temperaturføler sidder på en almindelig indgang på et inputmodul og
// tænder og slukker indgangen i et langsomt mønster. Hver bit varer ca.
// 122 ms:
//   "1": tændt ca. 81 ms, derefter slukket ca. 41 ms
//   "0": tændt ca. 41 ms, derefter slukket ca. 81 ms
//
// En pakke er 41 bit, mest betydende bit først:
//   12 bit rumtemperatur, 12 bit luftfugtighed, 12 bit gulvtemperatur
//   (alle i tiendedele), og til sidst 5 bit kontrolsum.
// Kontrolsummen er summen af de ni 4-bit-grupper i de tre værdier, de
// laveste 4 bit. Uden gulvføler sættes den øverste (5.) bit, og 4. bit
// vendes.
//
// Indgangen ses kun, når inputmodulet sender en frame (ca. hvert 19. ms),
// så pulslængderne måles med den opløsning. Afkoderen får tidspunkter for
// hver ændring af indgangen og tolker pulsernes længde.
#pragma once

#include <cstdint>

namespace ihc {

constexpr uint32_t kTempBits = 41;

struct TemperaturMaaling {
  int16_t temperatur = 0;  // tiendedele °C
  bool har_fugt = false;
  uint16_t fugt = 0;       // tiendedele %
  bool har_gulv = false;
  int16_t gulv = 0;        // tiendedele °C
};

// Kontrolsum for en pakke (5 bit).
uint8_t temperatur_kontrolsum(uint16_t temp, uint16_t fugt, uint16_t gulv, bool har_gulv);

// Bygger en pakke som 41 bit (bit 40 sendes først). Bruges i tests.
uint64_t temperatur_pakke(int16_t temp, uint16_t fugt, int16_t gulv, bool har_gulv);

// Tolker 41 modtagne bit. Returnerer false, hvis kontrolsummen ikke passer.
bool temperatur_tolk(uint64_t bits, TemperaturMaaling &ud);

class TemperaturModtager {
 public:
  // Pulslængder i ms. Tolerancen dækker, at indgangen kun ses ca. hvert
  // 19. ms, og at følerens takt kan afvige. Bitstarterne forudsiges ud fra
  // følerens faste takt, og grænsen mellem kort og lang følger den målte
  // bitlængde, så en føler, der går for hurtigt eller langsomt, afkodes.
  static constexpr uint32_t kKortMinMs = 15;    // nominelt 41 ms
  static constexpr uint32_t kPeriodeMs = 122;   // nominel bitlængde; grænsen er det halve
  static constexpr uint32_t kLangMaxMs = 140;   // nominelt 81 ms
  static constexpr uint32_t kPauseMinMs = 250;  // slukket så længe = mellem to pakker
  static constexpr uint32_t kMaxPauseMs = 30000; // længere pause: føleren har været væk
  static constexpr int kMaxSpringX10 = 50;       // højst 5 °C forskel mellem to pakker i træk

  // Kaldes ved hver ændring af indgangen. tid_ms må gerne løbe rundt.
  void kant(uint32_t tid_ms, bool taendt);

  // Sand én gang pr. ny, gyldig måling. Efter opstart, eller når føleren
  // har været væk, kræves to gyldige pakker i træk; den første bruges kun
  // til at bekræfte, at pakkestarten er fundet rigtigt.
  bool ny_maaling(TemperaturMaaling &ud);

  uint32_t gyldige() const { return gyldige_; }
  uint32_t fejl() const { return fejl_; }

 private:
  void tab_synkronisering();
  void pakke_slut(const TemperaturMaaling &m);
  static bool realistisk(const TemperaturMaaling &m);  // -50 til 100 °C, 0-100 %
  static bool ligner(const TemperaturMaaling &a, const TemperaturMaaling &b);

  bool niveau_ = false;
  bool har_tid_ = false;
  uint32_t sidste_tid_ = 0;
  // Følerens takt (faselåst sløjfe). Tider i 1/8 ms.
  uint32_t periode_x8_ = kPeriodeMs * 8;   // bitlængde
  int32_t start_x8_ = 0;                   // beregnet start af nuværende bit
  int32_t maalt_start_x8_ = 0;             // målt start af nuværende bit
  bool har_start_ = false;
  bool synkron_ = false;     // pakkestart kendt (efter pause eller forrige pakke)
  bool bekraeftet_ = false;  // forrige pakke var gyldig
  uint32_t antal_ = 0;
  uint64_t bits_ = 0;
  bool ny_ = false;
  TemperaturMaaling maaling_;
  TemperaturMaaling forrige_;  // seneste gyldige pakke
  uint32_t gyldige_ = 0;
  uint32_t fejl_ = 0;
};

}  // namespace ihc
