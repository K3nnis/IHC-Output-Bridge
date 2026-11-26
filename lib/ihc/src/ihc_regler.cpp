#include "ihc_regler.h"

namespace ihc {

bool regel_gyldig(const Regel &r) {
  return r.type == RegelType::kKip && r.in_module < kInputModules && r.in_channel < kInputsPerModule &&
         r.out_module < kOutputModules && r.out_channel < kOutputsPerModule;
}

size_t Regeltabel::build(const Regel *regler, size_t antal) {
  for (auto &row : map_)
    for (auto &b : row) b = 0;
  antal_ = 0;
  for (size_t i = 0; i < antal && antal_ < kMaxRegler; ++i) {
    const Regel &r = regler[i];
    if (!regel_gyldig(r)) continue;
    // OR: samme regel to gange kipper kun én gang.
    map_[r.in_module * kInputsPerModule + r.in_channel][r.out_module] |=
        static_cast<uint8_t>(1u << r.out_channel);
    ++antal_;
  }
  return antal_;
}

IHC_IRAM uint16_t Regeltabel::apply(uint32_t in_module, uint16_t rising, uint8_t outputs[kOutputModules]) const {
  uint16_t changed = 0;
  if (in_module >= kInputModules) return 0;
  for (uint32_t c = 0; c < kInputsPerModule; ++c) {
    if (!(rising & (1u << c))) continue;
    const uint8_t *row = map_[in_module * kInputsPerModule + c];
    for (uint32_t m = 0; m < kOutputModules; ++m) {
      if (row[m]) {
        outputs[m] ^= row[m];
        changed |= static_cast<uint16_t>(1u << m);
      }
    }
  }
  return changed;
}

size_t regler_til_bytes(const Regel *regler, size_t antal, uint8_t *buf, size_t buf_size) {
  const size_t need = kReglerHeaderBytes + antal * kReglerBytesPerRegel;
  if (antal > 0xFFFF || need > buf_size) return 0;
  buf[0] = kReglerFormatVersion;
  buf[1] = static_cast<uint8_t>(antal & 0xFF);
  buf[2] = static_cast<uint8_t>(antal >> 8);
  uint8_t *p = buf + kReglerHeaderBytes;
  for (size_t i = 0; i < antal; ++i) {
    *p++ = static_cast<uint8_t>(regler[i].type);
    *p++ = regler[i].in_module;
    *p++ = regler[i].in_channel;
    *p++ = regler[i].out_module;
    *p++ = regler[i].out_channel;
  }
  return need;
}

int regler_fra_bytes(const uint8_t *buf, size_t len, Regel *regler, size_t max_antal) {
  if (len < kReglerHeaderBytes || buf[0] != kReglerFormatVersion) return -1;
  const size_t antal = buf[1] | (static_cast<size_t>(buf[2]) << 8);
  if (len != kReglerHeaderBytes + antal * kReglerBytesPerRegel || antal > max_antal) return -1;
  const uint8_t *p = buf + kReglerHeaderBytes;
  for (size_t i = 0; i < antal; ++i) {
    regler[i].type = static_cast<RegelType>(p[0]);
    regler[i].in_module = p[1];
    regler[i].in_channel = p[2];
    regler[i].out_module = p[3];
    regler[i].out_channel = p[4];
    if (!regel_gyldig(regler[i])) return -1;
    p += kReglerBytesPerRegel;
  }
  return static_cast<int>(antal);
}

}  // namespace ihc
