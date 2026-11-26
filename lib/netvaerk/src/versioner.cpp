#include "versioner.h"

#include <cctype>
#include <vector>

namespace netvaerk {

namespace {

struct Version {
  long dele[3] = {0, 0, 0};
  std::vector<std::string> pre;  // tom = færdig version
};

bool kun_tal(const std::string &s) {
  if (s.empty()) return false;
  for (char c : s)
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  return true;
}

Version fortolk(std::string s) {
  Version v;
  if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
  const size_t plus = s.find('+');  // byggedata ignoreres
  if (plus != std::string::npos) s.erase(plus);
  const size_t streg = s.find('-');
  std::string kerne = s.substr(0, streg);
  if (streg != std::string::npos) {
    std::string rest = s.substr(streg + 1);
    size_t start = 0;
    while (start <= rest.size()) {
      const size_t punkt = rest.find('.', start);
      v.pre.push_back(rest.substr(start, punkt == std::string::npos ? std::string::npos : punkt - start));
      if (punkt == std::string::npos) break;
      start = punkt + 1;
    }
  }
  size_t pos = 0;
  for (int i = 0; i < 3 && pos <= kerne.size(); ++i) {
    const size_t punkt = kerne.find('.', pos);
    const std::string del = kerne.substr(pos, punkt == std::string::npos ? std::string::npos : punkt - pos);
    v.dele[i] = kun_tal(del) ? std::stol(del) : 0;
    if (punkt == std::string::npos) break;
    pos = punkt + 1;
  }
  return v;
}

// Sammenligner to identifikatorer i en testversion. "dev" er ældst.
int sammenlign_id(const std::string &a, const std::string &b) {
  const bool a_dev = a == "dev", b_dev = b == "dev";
  if (a_dev || b_dev) return a_dev == b_dev ? 0 : (a_dev ? -1 : 1);
  const bool a_tal = kun_tal(a), b_tal = kun_tal(b);
  if (a_tal && b_tal) {
    const long x = std::stol(a), y = std::stol(b);
    return x < y ? -1 : (x > y ? 1 : 0);
  }
  if (a_tal != b_tal) return a_tal ? -1 : 1;  // tal er ældre end tekst
  return a < b ? -1 : (a > b ? 1 : 0);
}

}  // namespace

int sammenlign_versioner(const std::string &a, const std::string &b) {
  const Version x = fortolk(a), y = fortolk(b);
  for (int i = 0; i < 3; ++i) {
    if (x.dele[i] != y.dele[i]) return x.dele[i] < y.dele[i] ? -1 : 1;
  }
  // Samme nummer: en færdig version er nyere end en testversion.
  if (x.pre.empty() || y.pre.empty()) {
    if (x.pre.empty() && y.pre.empty()) return 0;
    return x.pre.empty() ? 1 : -1;
  }
  for (size_t i = 0; i < x.pre.size() && i < y.pre.size(); ++i) {
    const int r = sammenlign_id(x.pre[i], y.pre[i]);
    if (r) return r;
  }
  if (x.pre.size() == y.pre.size()) return 0;
  return x.pre.size() < y.pre.size() ? -1 : 1;
}

bool er_testversion(const std::string &v) { return v.find('-') != std::string::npos; }

}  // namespace netvaerk
