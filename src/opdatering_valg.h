// Valg af release ud fra GitHubs svar på GET /repos/<repo>/releases.
// Kun cJSON og standardbiblioteket, så det kan testes på PC.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace opdatering {

struct Release {
  std::string version;  // uden "v", f.eks. "2.0.0-beta.2"
  std::string navn;     // f.eks. "IHC Bridge 2.0.0-beta.2"
  std::string dato;     // ISO-dato, f.eks. "2026-11-27"
  std::string side;     // releasens side på GitHub
  std::string noter;    // ændringsnoter (forkortet)
  std::string fil;      // download-adresse for -ota.bin
  uint32_t stoerrelse = 0;
  bool test = false;    // pre-release
};

// Finder den nyeste release med en "-ota.bin"-fil fra netop dette repo.
// Testversioner (pre-releases) tages kun med, hvis testversioner er sand.
// Returnerer false med fejl, hvis svaret ikke kan læses, og false uden
// fejl, hvis der ikke findes nogen passende release.
bool vaelg_release(const char *json, size_t len, const char *repo, bool testversioner, Release &ud, std::string &fejl);

}  // namespace opdatering
