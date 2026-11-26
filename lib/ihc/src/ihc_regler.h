// Regler: lokal kobling fra indgange til udgange.
//
// Reglerne oversættes til en Regeltabel, som bus-interruptet kan slå op i
// på få mikrosekunder: for hver af de 128 indgange findes et bitsæt over
// de 128 udgange, der skal kippes (toggles), når indgangen går fra OFF til ON.
#pragma once

#include <cstddef>
#include <cstdint>

#include "ihc_protocol.h"

namespace ihc {

constexpr uint32_t kInputModules = 8;
constexpr uint32_t kInputsPerModule = 16;
constexpr uint32_t kOutputModules = 16;
constexpr uint32_t kOutputsPerModule = 8;

enum class RegelType : uint8_t {
  kKip = 0,  // tryk tænder, næste tryk slukker
};

// Moduler og kanaler tælles fra 0 internt (modul 1 = 0, kanal 1 = 0).
struct Regel {
  RegelType type;
  uint8_t in_module;    // 0-7
  uint8_t in_channel;   // 0-15
  uint8_t out_module;   // 0-15
  uint8_t out_channel;  // 0-7
};

bool regel_gyldig(const Regel &r);

class Regeltabel {
 public:
  static constexpr size_t kMaxRegler = 512;

  // Bygger tabellen. Ugyldige regler springes over. Returnerer antal brugte.
  size_t build(const Regel *regler, size_t antal);

  // Kaldes fra interruptet. rising: indgange i modulet, der netop er gået
  // fra OFF til ON. Kipper de berørte udgange i outputs[] og returnerer et
  // bitsæt over de outputmoduler, der er ændret.
  uint16_t apply(uint32_t in_module, uint16_t rising, uint8_t outputs[kOutputModules]) const;

  bool empty() const { return antal_ == 0; }
  size_t antal() const { return antal_; }

 private:
  // map_[indgang][outputmodul] = udgange der kippes
  uint8_t map_[kInputModules * kInputsPerModule][kOutputModules] = {};
  size_t antal_ = 0;
};

// Binært format til lagring (NVS): [version][antal lav][antal høj] + 5 byte pr. regel.
constexpr uint8_t kReglerFormatVersion = 1;
constexpr size_t kReglerHeaderBytes = 3;
constexpr size_t kReglerBytesPerRegel = 5;

// Returnerer antal skrevne bytes, eller 0 hvis bufferen er for lille.
size_t regler_til_bytes(const Regel *regler, size_t antal, uint8_t *buf, size_t buf_size);
// Returnerer antal læste regler, eller -1 ved ugyldige data.
int regler_fra_bytes(const uint8_t *buf, size_t len, Regel *regler, size_t max_antal);

}  // namespace ihc
