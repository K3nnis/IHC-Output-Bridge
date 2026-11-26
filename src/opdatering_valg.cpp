#include "opdatering_valg.h"

#include <cstring>

#include "cJSON.h"
#include "versioner.h"

namespace opdatering {

namespace {

constexpr size_t kMaxNoter = 2000;

std::string tekst(const cJSON *o, const char *navn) {
  const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, navn);
  return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

bool slutter_med(const std::string &s, const char *slut) {
  const size_t n = strlen(slut);
  return s.size() >= n && s.compare(s.size() - n, n, slut) == 0;
}

}  // namespace

bool vaelg_release(const char *json, size_t len, const char *repo, bool testversioner, Release &ud, std::string &fejl) {
  cJSON *rod = cJSON_ParseWithLength(json, len);
  if (!rod || !cJSON_IsArray(rod)) {
    cJSON_Delete(rod);
    fejl = "Uventet svar fra GitHub";
    return false;
  }
  // Kun filer, der ligger på dette repos releases, godtages.
  const std::string tilladt = std::string("https://github.com/") + repo + "/releases/download/";

  bool fundet = false;
  const cJSON *r;
  cJSON_ArrayForEach(r, rod) {
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "draft"))) continue;
    const bool test = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "prerelease"));
    if (test && !testversioner) continue;
    std::string version = tekst(r, "tag_name");
    if (!version.empty() && (version[0] == 'v' || version[0] == 'V')) version.erase(0, 1);
    if (version.empty()) continue;

    const cJSON *fil = nullptr;
    const cJSON *a;
    cJSON_ArrayForEach(a, cJSON_GetObjectItemCaseSensitive(r, "assets")) {
      const std::string url = tekst(a, "browser_download_url");
      if (slutter_med(tekst(a, "name"), "-ota.bin") && url.compare(0, tilladt.size(), tilladt) == 0) {
        fil = a;
        break;
      }
    }
    if (!fil) continue;
    if (fundet && !netvaerk::er_nyere(version, ud.version)) continue;

    ud = Release();
    ud.version = version;
    ud.navn = tekst(r, "name");
    ud.dato = tekst(r, "published_at").substr(0, 10);
    ud.side = tekst(r, "html_url");
    ud.noter = tekst(r, "body");
    if (ud.noter.size() > kMaxNoter) ud.noter = ud.noter.substr(0, kMaxNoter) + " …";
    ud.fil = tekst(fil, "browser_download_url");
    const cJSON *s = cJSON_GetObjectItemCaseSensitive(fil, "size");
    ud.stoerrelse = cJSON_IsNumber(s) ? static_cast<uint32_t>(s->valuedouble) : 0;
    ud.test = test;
    fundet = true;
  }
  cJSON_Delete(rod);
  return fundet;
}

}  // namespace opdatering
