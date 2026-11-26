// Opdatering: søger efter ny firmware på GitHub og installerer den.
//
// IHC Bridge spørger selv GitHubs API om releases i repoet herunder og
// henter "-ota.bin"-filen over HTTPS. Filen tjekkes (projektnavn og
// kontrolsum), før den aktiveres, og den nye firmware bekræftes som ved
// manuel opdatering: går den ned inden for det første minut, startes den
// forrige igen.
//
// Søgning og installation kører i en task på kerne 0; web-UI'et følger med
// via status().
#pragma once

#include <cstdint>
#include <string>

#include "opdatering_valg.h"

namespace opdatering {

// Repoet, der søges i. Ret her i en fork for at pege på dine egne releases.
constexpr const char *kRepo = "K3nnis/IHC-Output-Bridge";

enum class Tilstand : uint8_t {
  kKlar,        // ingen søgning endnu
  kSoeger,
  kNyeste,      // den installerede version er den nyeste
  kFundet,      // en nyere version er klar til installation
  kHenter,      // henter og installerer
  kGenstarter,
  kFejl,
};

struct Status {
  Tilstand tilstand = Tilstand::kKlar;
  std::string fejl;
  bool har_release = false;
  Release release;          // nyeste fundne release
  int procent = 0;          // under installation
  bool testversioner = false;
  int64_t sidst_soegt_s = -1;  // sekunder siden seneste søgning, -1 = aldrig
};

void init();  // kræver lager::init()

Status status();
// Starter en søgning. Returnerer false, hvis en søgning eller installation kører.
bool soeg();
// Installerer den fundne release. version skal være den, brugeren har
// bekræftet; den skal passe med den fundne.
bool installer(const std::string &version, std::string &fejl);
bool saet_testversioner(bool ja);

}  // namespace opdatering
