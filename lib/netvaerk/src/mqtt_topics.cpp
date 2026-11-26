#include "mqtt_topics.h"

#include <cctype>
#include <cstdio>

namespace netvaerk {

namespace {

std::string num(uint32_t v) { return std::to_string(v); }

std::string lower(std::string s) {
  for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

// Læser et positivt heltal (1-99) og flytter pos frem.
bool read_num(const std::string &s, size_t &pos, uint32_t &out) {
  size_t start = pos;
  uint32_t v = 0;
  while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos])) && pos - start < 3) {
    v = v * 10 + static_cast<uint32_t>(s[pos] - '0');
    ++pos;
  }
  if (pos == start || v == 0 || s[start] == '0') return false;
  out = v;
  return true;
}

std::string enhed_json(const Enhed &e) {
  std::string j = "{\"ids\":[\"" + json_escape(e.id) + "\"],\"name\":\"" + json_escape(e.navn) +
                  "\",\"mf\":\"IHC Bridge\",\"mdl\":\"ESP32-S3 IHC-controller\",\"sw\":\"" + json_escape(e.version) +
                  "\"";
  if (!e.url.empty()) j += ",\"cu\":\"" + json_escape(e.url) + "\"";
  return j + "}";
}

std::string objekt_id(const Enhed &e, const char *type, uint32_t modul, uint32_t kanal) {
  return e.id + "_" + type + "_" + num(modul) + "_" + num(kanal);
}

}  // namespace

std::string topic_status(const std::string &basis) { return basis + "/status"; }

std::string topic_udgang(const std::string &basis, uint32_t modul, uint32_t kanal) {
  return basis + "/output/" + num(modul) + "/" + num(kanal) + "/state";
}

std::string topic_udgang_set(const std::string &basis, uint32_t modul, uint32_t kanal) {
  return basis + "/output/" + num(modul) + "/" + num(kanal) + "/set";
}

std::string topic_udgang_set_alle(const std::string &basis) { return basis + "/output/+/+/set"; }

std::string topic_indgang(const std::string &basis, uint32_t modul, uint32_t kanal) {
  return basis + "/input/" + num(modul) + "/" + num(kanal) + "/state";
}

std::string topic_indgangsmodul_status(const std::string &basis, uint32_t modul) {
  return basis + "/input/" + num(modul) + "/status";
}

bool parse_udgang_set(const std::string &basis, const std::string &topic, uint32_t &modul, uint32_t &kanal) {
  const std::string prefix = basis + "/output/";
  if (topic.compare(0, prefix.size(), prefix) != 0) return false;
  size_t pos = prefix.size();
  uint32_t m = 0, k = 0;
  if (!read_num(topic, pos, m) || pos >= topic.size() || topic[pos] != '/') return false;
  ++pos;
  if (!read_num(topic, pos, k)) return false;
  if (topic.compare(pos, std::string::npos, "/set") != 0) return false;
  modul = m;
  kanal = k;
  return true;
}

Kommando parse_kommando(const std::string &payload, const std::string &payload_on,
                        const std::string &payload_off) {
  const std::string p = lower(trim(payload));
  if (p.empty()) return Kommando::kUgyldig;
  if (p == lower(payload_on) || p == "on" || p == "1" || p == "true") return Kommando::kTaend;
  if (p == lower(payload_off) || p == "off" || p == "0" || p == "false") return Kommando::kSluk;
  if (p == "toggle") return Kommando::kSkift;
  return Kommando::kUgyldig;
}

bool basis_topic_gyldig(const std::string &basis) {
  if (basis.empty() || basis.size() > 100 || basis.front() == '/' || basis.back() == '/') return false;
  for (char c : basis) {
    if (c == '+' || c == '#' || c == '\0' || c == ' ') return false;
  }
  return basis.find("//") == std::string::npos;
}

std::string discovery_topic_udgang(const std::string &prefix, const Enhed &e, uint32_t modul, uint32_t kanal) {
  return prefix + "/switch/" + e.id + "/udgang_" + num(modul) + "_" + num(kanal) + "/config";
}

std::string discovery_topic_indgang(const std::string &prefix, const Enhed &e, uint32_t modul, uint32_t kanal) {
  return prefix + "/binary_sensor/" + e.id + "/indgang_" + num(modul) + "_" + num(kanal) + "/config";
}

std::string discovery_udgang(const std::string &basis, const Enhed &e, uint32_t modul, uint32_t kanal,
                             const std::string &navn, const std::string &payload_on, const std::string &payload_off) {
  return "{\"name\":\"" + json_escape(navn) + "\",\"uniq_id\":\"" + objekt_id(e, "udgang", modul, kanal) +
         "\",\"obj_id\":\"" + objekt_id(e, "udgang", modul, kanal) + "\",\"stat_t\":\"" +
         json_escape(topic_udgang(basis, modul, kanal)) + "\",\"cmd_t\":\"" +
         json_escape(topic_udgang_set(basis, modul, kanal)) + "\",\"pl_on\":\"" + json_escape(payload_on) +
         "\",\"pl_off\":\"" + json_escape(payload_off) + "\",\"avty_t\":\"" + json_escape(topic_status(basis)) +
         "\",\"dev\":" + enhed_json(e) + "}";
}

std::string discovery_indgang(const std::string &basis, const Enhed &e, uint32_t modul, uint32_t kanal,
                              const std::string &navn, const std::string &payload_on, const std::string &payload_off) {
  return "{\"name\":\"" + json_escape(navn) + "\",\"uniq_id\":\"" + objekt_id(e, "indgang", modul, kanal) +
         "\",\"obj_id\":\"" + objekt_id(e, "indgang", modul, kanal) + "\",\"stat_t\":\"" +
         json_escape(topic_indgang(basis, modul, kanal)) + "\",\"pl_on\":\"" + json_escape(payload_on) +
         "\",\"pl_off\":\"" + json_escape(payload_off) + "\",\"avty\":[{\"t\":\"" +
         json_escape(topic_status(basis)) + "\"},{\"t\":\"" + json_escape(topic_indgangsmodul_status(basis, modul)) +
         "\"}],\"avty_mode\":\"all\",\"dev\":" + enhed_json(e) + "}";
}

std::string json_escape(const std::string &s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (unsigned char c : s) {
    if (c == '"') {
      out += "\\\"";
    } else if (c == '\\') {
      out += "\\\\";
    } else if (c < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04x", c);
      out += buf;
    } else {
      out += static_cast<char>(c);  // UTF-8 (æøå) gives videre uændret
    }
  }
  return out;
}

}  // namespace netvaerk
