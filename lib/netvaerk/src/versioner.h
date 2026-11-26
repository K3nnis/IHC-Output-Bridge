// Sammenligning af versionsnumre (semantisk versionering).
//
//   2.0.0 > 2.0.0-beta.2 > 2.0.0-beta.1 > 2.0.0-dev > 1.9.9
//
// Et "v" foran ignoreres (v2.0.0 = 2.0.0). Testversioner har en bindestreg
// (2.0.0-beta.1). Udviklerbuilds ("-dev") regnes for ældre end alle
// udgivne versioner med samme nummer.
#pragma once

#include <string>

namespace netvaerk {

// <0 hvis a er ældre end b, 0 hvis ens, >0 hvis a er nyere.
int sammenlign_versioner(const std::string &a, const std::string &b);

inline bool er_nyere(const std::string &kandidat, const std::string &nuvaerende) {
  return sammenlign_versioner(kandidat, nuvaerende) > 0;
}

// Sand for testversioner (med bindestreg), f.eks. "2.0.0-beta.1".
bool er_testversion(const std::string &v);

}  // namespace netvaerk
