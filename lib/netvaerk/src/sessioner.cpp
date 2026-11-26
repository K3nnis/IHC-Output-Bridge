#include "sessioner.h"

#include <cstring>

namespace netvaerk {

bool ens_konstant_tid(const uint8_t *a, const uint8_t *b, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; ++i) diff |= a[i] ^ b[i];
  return diff == 0;
}

std::string Sessioner::opret(int64_t nu, const uint8_t tilfaeldig[32]) {
  static const char *hex = "0123456789abcdef";
  int plads = 0;
  for (int i = 0; i < kMax; ++i) {
    if (!s_[i].aktiv || nu - s_[i].sidst_brugt > kUdloebMs) {
      plads = i;
      break;
    }
    if (s_[i].sidst_brugt < s_[plads].sidst_brugt) plads = i;
  }
  Session &s = s_[plads];
  for (int i = 0; i < 32; ++i) {
    s.token[i * 2] = hex[tilfaeldig[i] >> 4];
    s.token[i * 2 + 1] = hex[tilfaeldig[i] & 0x0F];
  }
  s.token[64] = '\0';
  s.sidst_brugt = nu;
  s.aktiv = true;
  return s.token;
}

bool Sessioner::gyldig(const std::string &token, int64_t nu) {
  if (token.size() != 64) return false;
  bool fundet = false;
  for (auto &s : s_) {
    if (!s.aktiv) continue;
    if (nu - s.sidst_brugt > kUdloebMs) {
      s.aktiv = false;
      continue;
    }
    // Alle aktive sessioner sammenlignes, så tidsforbruget ikke afslører noget.
    if (ens_konstant_tid(reinterpret_cast<const uint8_t *>(s.token),
                         reinterpret_cast<const uint8_t *>(token.data()), 64)) {
      s.sidst_brugt = nu;
      fundet = true;
    }
  }
  return fundet;
}

void Sessioner::slet(const std::string &token) {
  for (auto &s : s_) {
    if (s.aktiv && token.size() == 64 && memcmp(s.token, token.data(), 64) == 0) s.aktiv = false;
  }
}

void Sessioner::slet_alle() {
  for (auto &s : s_) s.aktiv = false;
}

int Sessioner::antal(int64_t nu) const {
  int n = 0;
  for (const auto &s : s_) {
    if (s.aktiv && nu - s.sidst_brugt <= kUdloebMs) ++n;
  }
  return n;
}

int64_t LoginBremse::ventetid_ms(int64_t nu) const { return laast_til_ > nu ? laast_til_ - nu : 0; }

void LoginBremse::fejl(int64_t nu) {
  ++fejl_;
  if (fejl_ < kFrieForsoeg) return;
  // 30 s efter 5. fejl, derefter dobbelt så længe for hver fejl, højst 15 min.
  int64_t vent = kFoersteVentMs;
  for (int i = kFrieForsoeg; i < fejl_ && vent < kMaxVentMs; ++i) vent *= 2;
  if (vent > kMaxVentMs) vent = kMaxVentMs;
  laast_til_ = nu + vent;
}

void LoginBremse::succes() {
  fejl_ = 0;
  laast_til_ = 0;
}

}  // namespace netvaerk
