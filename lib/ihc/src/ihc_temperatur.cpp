#include "ihc_temperatur.h"

namespace ihc {

namespace {

uint32_t nibble_sum(uint16_t v) { return (v & 0xF) + ((v >> 4) & 0xF) + ((v >> 8) & 0xF); }

// 12 bit med fortegn (to-komplement): -204,8 til 204,7 °C.
int16_t med_fortegn(uint16_t v) { return v & 0x800 ? static_cast<int16_t>(v) - 0x1000 : static_cast<int16_t>(v); }

constexpr uint64_t kMaske = (1ULL << kTempBits) - 1;

}  // namespace

uint8_t temperatur_kontrolsum(uint16_t temp, uint16_t fugt, uint16_t gulv, bool har_gulv) {
  uint8_t sum = static_cast<uint8_t>((nibble_sum(temp & 0xFFF) + nibble_sum(fugt & 0xFFF) + nibble_sum(gulv & 0xFFF)) & 0xF);
  if (!har_gulv) sum = static_cast<uint8_t>(0x10 | (sum ^ 0x08));
  return sum;
}

uint64_t temperatur_pakke(int16_t temp, uint16_t fugt, int16_t gulv, bool har_gulv) {
  const uint16_t t = static_cast<uint16_t>(temp) & 0xFFF;
  const uint16_t f = fugt & 0xFFF;
  const uint16_t g = har_gulv ? static_cast<uint16_t>(gulv) & 0xFFF : 0;
  return (static_cast<uint64_t>(t) << 29) | (static_cast<uint64_t>(f) << 17) | (static_cast<uint64_t>(g) << 5) |
         temperatur_kontrolsum(t, f, g, har_gulv);
}

bool temperatur_tolk(uint64_t bits, TemperaturMaaling &ud) {
  const uint16_t t = (bits >> 29) & 0xFFF;
  const uint16_t f = (bits >> 17) & 0xFFF;
  const uint16_t g = (bits >> 5) & 0xFFF;
  const uint8_t chk = bits & 0x1F;
  bool har_gulv;
  if (chk == temperatur_kontrolsum(t, f, g, true)) {
    har_gulv = true;
  } else if (g == 0 && chk == temperatur_kontrolsum(t, f, g, false)) {
    har_gulv = false;  // uden gulvføler sendes gulvfeltet som 0
  } else {
    return false;
  }
  ud.temperatur = med_fortegn(t);
  ud.fugt = f;
  ud.har_fugt = f != 0;  // en føler uden fugtmåler sender 0
  ud.har_gulv = har_gulv;
  ud.gulv = har_gulv ? med_fortegn(g) : 0;
  return true;
}

bool TemperaturModtager::realistisk(const TemperaturMaaling &m) {
  return m.temperatur >= -500 && m.temperatur <= 1000 && m.fugt <= 1000 &&
         (!m.har_gulv || (m.gulv >= -500 && m.gulv <= 1000));
}

void TemperaturModtager::tab_synkronisering() {
  if (antal_ > 0 || bekraeftet_) ++fejl_;
  synkron_ = false;
  bekraeftet_ = false;
  antal_ = 0;
  bits_ = 0;
}

bool TemperaturModtager::ligner(const TemperaturMaaling &a, const TemperaturMaaling &b) {
  auto taet = [](int a, int b) { return a - b <= kMaxSpringX10 && b - a <= kMaxSpringX10; };
  return a.har_fugt == b.har_fugt && a.har_gulv == b.har_gulv && taet(a.temperatur, b.temperatur) &&
         (!a.har_gulv || taet(a.gulv, b.gulv));
}

void TemperaturModtager::pakke_slut(const TemperaturMaaling &m) {
  // En pakke godtages kun, når pakken før den også var gyldig og ligner den
  // (samme felter, højst 5 °C forskel). Det forhindrer, at støj eller en
  // forkert pakkestart giver falske målinger.
  if (bekraeftet_ && ligner(forrige_, m)) {
    maaling_ = m;
    ny_ = true;
    ++gyldige_;
  }
  forrige_ = m;
  synkron_ = true;
  bekraeftet_ = true;
  antal_ = 0;
  bits_ = 0;
}

void TemperaturModtager::kant(uint32_t tid_ms, bool taendt) {
  if (!har_tid_) {
    // Første kant: længden af det foregående niveau kendes ikke.
    har_tid_ = true;
    niveau_ = taendt;
    sidste_tid_ = tid_ms;
    return;
  }
  if (taendt == niveau_) return;
  const uint32_t varighed = tid_ms - sidste_tid_;
  sidste_tid_ = tid_ms;
  niveau_ = taendt;

  if (taendt) {
    // En ny bit begynder. Føleren sender med fast takt, så starten af hver
    // bit kan forudsiges ud fra de foregående. Det beregnede starttidspunkt
    // er mere præcist end det målte, som kan være op til en frame (19 ms)
    // forsinket.
    const int32_t t_x8 = static_cast<int32_t>(tid_ms * 8u);
    if (har_start_ && varighed <= kLangMaxMs) {
      const int32_t forventet = start_x8_ + static_cast<int32_t>(periode_x8_);
      const int32_t afvig = t_x8 - forventet;
      if (afvig > -static_cast<int32_t>(periode_x8_ / 2) && afvig < static_cast<int32_t>(periode_x8_ / 2)) {
        start_x8_ = forventet + afvig / 4;
        // Bitlængden følges via afstanden mellem de målte bitstarter.
        const int32_t maalt = t_x8 - maalt_start_x8_;
        periode_x8_ = static_cast<uint32_t>(static_cast<int32_t>(periode_x8_) + (maalt - static_cast<int32_t>(periode_x8_)) / 8);
      } else {
        start_x8_ = t_x8;
      }
    } else {
      start_x8_ = t_x8;  // første bit efter pause eller fejl
    }
    maalt_start_x8_ = t_x8;
    har_start_ = true;

    if (varighed >= kPauseMinMs) {
      // Pause mellem to pakker: en ny pakke begynder nu. Har føleren været
      // væk længe, skal der bekræftes forfra.
      if (antal_ > 0) ++fejl_;
      const bool behold = bekraeftet_ && varighed < kMaxPauseMs;
      synkron_ = true;
      bekraeftet_ = behold;
      antal_ = 0;
      bits_ = 0;
    } else if (varighed > kLangMaxMs) {
      tab_synkronisering();
    }
    return;
  }

  // Et tændt stykke er slut: dets længde er bittens værdi (kort = 0,
  // lang = 1). Længden regnes fra den beregnede bitstart, og grænsen er
  // halvdelen af den målte bitlængde (nominelt 61 ms).
  const int32_t hoej_x8 = har_start_ ? static_cast<int32_t>(tid_ms * 8u) - start_x8_ : static_cast<int32_t>(varighed * 8);
  const int32_t graense_x8 = static_cast<int32_t>(periode_x8_ / 2);
  bool bit;
  if (hoej_x8 >= static_cast<int32_t>(kKortMinMs * 8) && hoej_x8 <= graense_x8) {
    bit = false;
  } else if (hoej_x8 > graense_x8 && hoej_x8 <= static_cast<int32_t>(kLangMaxMs * 8)) {
    bit = true;
  } else {
    tab_synkronisering();
    return;
  }
  bits_ = ((bits_ << 1) | (bit ? 1u : 0u)) & kMaske;
  ++antal_;
  if (antal_ < kTempBits) return;

  TemperaturMaaling m;
  const bool gyldig = temperatur_tolk(bits_, m) && realistisk(m);
  if (synkron_) {
    // Kendt pakkestart: præcis 41 bit skal give en gyldig pakke.
    if (gyldig) {
      pakke_slut(m);
    } else {
      tab_synkronisering();
    }
  } else if (gyldig) {
    // Ukendt pakkestart: hver position prøves, til én passer.
    bekraeftet_ = false;
    pakke_slut(m);
  }
}

bool TemperaturModtager::ny_maaling(TemperaturMaaling &ud) {
  if (!ny_) return false;
  ud = maaling_;
  ny_ = false;
  return true;
}

}  // namespace ihc
