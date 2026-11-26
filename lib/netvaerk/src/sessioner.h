// Sessioner og login-bremse til web-UI'et. Ren logik, testet på PC.
//
// En session er et tilfældigt token på 32 bytes (64 hex-tegn), som browseren
// gemmer i en cookie. Sessioner udløber efter 7 dages inaktivitet og ved
// genstart af enheden. Login-bremsen gør gætteri på adgangskoden langsomt.
#pragma once

#include <cstdint>
#include <string>

namespace netvaerk {

class Sessioner {
 public:
  static constexpr int kMax = 8;
  static constexpr int64_t kUdloebMs = 7LL * 24 * 60 * 60 * 1000;

  // Opretter en session ud fra 32 tilfældige bytes. Er der fuldt, erstattes
  // den session, der har været ubrugt længst.
  std::string opret(int64_t nu, const uint8_t tilfaeldig[32]);
  // Sand hvis tokenet er en aktiv session. Forlænger sessionen.
  bool gyldig(const std::string &token, int64_t nu);
  void slet(const std::string &token);
  void slet_alle();
  int antal(int64_t nu) const;

 private:
  struct Session {
    char token[65] = {};
    int64_t sidst_brugt = 0;
    bool aktiv = false;
  };
  Session s_[kMax];
};

class LoginBremse {
 public:
  static constexpr int kFrieForsoeg = 5;
  static constexpr int64_t kFoersteVentMs = 30 * 1000;
  static constexpr int64_t kMaxVentMs = 15 * 60 * 1000;

  // 0 hvis login må forsøges nu, ellers ventetid i ms.
  int64_t ventetid_ms(int64_t nu) const;
  void fejl(int64_t nu);
  void succes();

 private:
  int fejl_ = 0;
  int64_t laast_til_ = 0;
};

// Sammenligner uden at afsløre via tidsforbrug, hvor strengene afviger.
bool ens_konstant_tid(const uint8_t *a, const uint8_t *b, size_t len);

}  // namespace netvaerk
