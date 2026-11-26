#include "logbog.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

namespace logbog {
namespace {

constexpr uint32_t kLinjer = 256;
constexpr size_t kLinjeLaengde = 160;

struct Linje {
  uint32_t nr;
  char tekst[kLinjeLaengde];
};

Linje *s_ring = nullptr;  // i PSRAM
uint32_t s_naeste = 1;
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
vprintf_like_t s_original = nullptr;

// Fjerner farvekoder (ESC[...m) og linjeskift.
void rens(char *s) {
  char *ud = s;
  for (char *p = s; *p; ++p) {
    if (*p == '\033') {
      while (*p && *p != 'm') ++p;
      if (!*p) break;
      continue;
    }
    if (*p == '\n' || *p == '\r') continue;
    *ud++ = *p;
  }
  *ud = '\0';
}

int hook(const char *fmt, va_list args) {
  va_list kopi;
  va_copy(kopi, args);
  const int r = s_original ? s_original(fmt, args) : vprintf(fmt, args);
  if (s_ring) {
    char buf[kLinjeLaengde];
    vsnprintf(buf, sizeof(buf), fmt, kopi);
    rens(buf);
    if (buf[0]) {
      portENTER_CRITICAL(&s_lock);
      Linje &l = s_ring[s_naeste % kLinjer];
      l.nr = s_naeste++;
      memcpy(l.tekst, buf, sizeof(buf));
      portEXIT_CRITICAL(&s_lock);
    }
  }
  va_end(kopi);
  return r;
}

}  // namespace

void init() {
  s_ring = static_cast<Linje *>(heap_caps_calloc(kLinjer, sizeof(Linje), MALLOC_CAP_SPIRAM));
  s_original = esp_log_set_vprintf(hook);
}

uint32_t hent(uint32_t efter, const std::function<void(uint32_t nr, const char *tekst)> &fn) {
  if (!s_ring) return 0;
  portENTER_CRITICAL(&s_lock);
  const uint32_t sidste = s_naeste - 1;
  portEXIT_CRITICAL(&s_lock);
  uint32_t fra = sidste >= kLinjer ? sidste - kLinjer + 1 : 1;
  if (efter + 1 > fra) fra = efter + 1;
  for (uint32_t nr = fra; nr <= sidste; ++nr) {
    Linje l;
    portENTER_CRITICAL(&s_lock);
    l = s_ring[nr % kLinjer];
    portEXIT_CRITICAL(&s_lock);
    if (l.nr == nr) fn(nr, l.tekst);
  }
  return sidste;
}

}  // namespace logbog
