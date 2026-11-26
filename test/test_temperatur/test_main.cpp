// Unit-tests af afkodning af IHC-temperaturfølere. Kører på PC: pio test -e native
#include <unity.h>

#include <cstdio>
#include <memory>
#include <random>
#include <vector>

#include "ihc_temperatur.h"

using namespace ihc;

// ---------------------------------------------------------------------------
// Signal fra føleren, som bridgen ser det
// ---------------------------------------------------------------------------

struct Signal {
  std::vector<std::pair<double, bool>> stykker;  // (varighed ms, tændt)
  double laengde = 0;
  void add(double ms, bool t) {
    stykker.push_back({ms, t});
    laengde += ms;
  }
  // En pakke som føleren sender den. skala > 1 = langsommere føler.
  void pakke(uint64_t bits, double skala = 1.0) {
    for (int i = kTempBits - 1; i >= 0; --i) {
      const bool b = (bits >> i) & 1;
      add((b ? 81 : 41) * skala, true);
      add((b ? 41 : 81) * skala, false);
    }
  }
  bool niveau(double t) const {
    double slut = 0;
    for (const auto &s : stykker) {
      slut += s.first;
      if (t < slut) return s.second;
    }
    return false;
  }
};

// Inputmodulet aflæser indgangen ved hver frame (ca. hvert 19. ms). For
// temperaturkanaler gives hver gyldig frame videre (uden Modtagers krav om
// to ens frames), og afkoderen får tidspunktet for den frame, hvor
// indgangen har ændret sig.
static void foed(TemperaturModtager &rx, const Signal &s, double fase_ms, double frame_ms = 19.03) {
  bool sidste = false, foerste = true;
  for (double t = fase_ms; t < s.laengde + 400; t += frame_ms) {
    const bool v = s.niveau(t);
    if (foerste || v != sidste) rx.kant(static_cast<uint32_t>(t), v);
    foerste = false;
    sidste = v;
  }
}

static std::vector<TemperaturMaaling> alle(TemperaturModtager &rx) {
  // ny_maaling giver kun den seneste; testene kalder derfor foed pakke for pakke,
  // eller tjekker blot den seneste.
  std::vector<TemperaturMaaling> v;
  TemperaturMaaling m;
  while (rx.ny_maaling(m)) v.push_back(m);
  return v;
}

// ---------------------------------------------------------------------------
// Pakkeformat
// ---------------------------------------------------------------------------

void test_kontrolsum_eksempel() {
  // 21,5 °C, ingen fugt, gulv 19,2 °C: firbit-grupperne 1101+0111+1100 = 32 -> 0.
  TEST_ASSERT_EQUAL_HEX8(0x00, temperatur_kontrolsum(215, 0, 192, true));
  // Uden gulvføler: 5. bit sættes, og 4. bit vendes.
  const uint8_t uden = temperatur_kontrolsum(215, 0, 0, false);
  const uint8_t sum = (13 + 7) & 0xF;  // 20 -> 4
  TEST_ASSERT_EQUAL_HEX8(0x10 | (sum ^ 0x08), uden);
}

void test_pakke_frem_og_tilbage() {
  struct {
    int16_t t;
    uint16_t f;
    int16_t g;
    bool hg;
  } tilfaelde[] = {{215, 0, 192, true}, {215, 0, 0, false}, {231, 455, 0, false}, {-75, 0, 0, false}, {0, 1000, 285, true}};
  for (const auto &c : tilfaelde) {
    TemperaturMaaling m;
    TEST_ASSERT_TRUE(temperatur_tolk(temperatur_pakke(c.t, c.f, c.g, c.hg), m));
    TEST_ASSERT_EQUAL_INT16(c.t, m.temperatur);
    TEST_ASSERT_EQUAL_UINT16(c.f, m.fugt);
    TEST_ASSERT_EQUAL(c.f != 0, m.har_fugt);
    TEST_ASSERT_EQUAL(c.hg, m.har_gulv);
    if (c.hg) TEST_ASSERT_EQUAL_INT16(c.g, m.gulv);
  }
}

void test_forkert_bit_afvises() {
  const uint64_t p = temperatur_pakke(215, 0, 192, true);
  TemperaturMaaling m;
  for (int i = 0; i < 41; ++i) {
    // En enkelt vendt bit må aldrig give en anden gyldig temperatur end den sendte.
    const bool ok = temperatur_tolk(p ^ (1ULL << i), m);
    if (ok) TEST_FAIL_MESSAGE("en vendt bit gav en gyldig pakke");
  }
}

// ---------------------------------------------------------------------------
// Afkodning af signalet
// ---------------------------------------------------------------------------

void test_pakker_med_pause() {
  std::mt19937 rng(3);
  for (int rep = 0; rep < 40; ++rep) {
    auto rx = std::make_unique<TemperaturModtager>();
    Signal s;
    s.add(500, false);
    for (int i = 0; i < 3; ++i) {
      s.pakke(temperatur_pakke(215 + i, 0, 192, true));
      s.add(400, false);
    }
    foed(*rx, s, (rng() % 1900) / 100.0);
    TemperaturMaaling m;
    // Første pakke bekræfter synkroniseringen; de to næste giver målinger.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, rx->gyldige(), "forventede 2 målinger");
    TEST_ASSERT_TRUE(rx->ny_maaling(m));
    TEST_ASSERT_EQUAL_INT16(217, m.temperatur);
    TEST_ASSERT_TRUE(m.har_gulv);
    TEST_ASSERT_EQUAL_INT16(192, m.gulv);
  }
}

void test_pakker_uden_pause_og_start_midt_i() {
  std::mt19937 rng(5);
  for (int rep = 0; rep < 40; ++rep) {
    auto rx = std::make_unique<TemperaturModtager>();
    Signal s;
    // Bridgen starter midt i en pakke, og føleren holder ingen pause.
    Signal fuld;
    fuld.pakke(temperatur_pakke(199, 512, 0, false));
    for (size_t i = 17; i < fuld.stykker.size(); ++i) s.add(fuld.stykker[i].first, fuld.stykker[i].second);
    for (int i = 0; i < 4; ++i) s.pakke(temperatur_pakke(200 + i, 512, 0, false));
    foed(*rx, s, (rng() % 1900) / 100.0);
    TemperaturMaaling m;
    TEST_ASSERT_TRUE_MESSAGE(rx->gyldige() >= 2, "forventede mindst 2 målinger");
    TEST_ASSERT_TRUE(rx->ny_maaling(m));
    TEST_ASSERT_EQUAL_INT16(203, m.temperatur);
    TEST_ASSERT_TRUE(m.har_fugt);
    TEST_ASSERT_EQUAL_UINT16(512, m.fugt);
    TEST_ASSERT_FALSE(m.har_gulv);
  }
}

void test_foelerens_takt_kan_afvige() {
  // Føleren må gå fra 20 % for hurtigt til 30 % for langsomt. Efter opstart
  // kan op til to pakker gå til at finde takten; derefter afkodes alle.
  const double skalaer[] = {0.8, 0.9, 0.93, 0.95, 1.05, 1.1, 1.2, 1.3};
  for (double sk : skalaer) {
    for (double fase = 0; fase < 19; fase += 2.3) {
      auto rx = std::make_unique<TemperaturModtager>();
      Signal s;
      s.add(500, false);
      for (int i = 0; i < 10; ++i) {
        s.pakke(temperatur_pakke(-42, 0, 0, false), sk);
        s.add(400, false);
      }
      foed(*rx, s, fase);
      char msg[64];
      snprintf(msg, sizeof(msg), "takt %.2f fase %.1f: %u af 10", sk, fase, (unsigned)rx->gyldige());
      TEST_ASSERT_TRUE_MESSAGE(rx->gyldige() >= 8, msg);
      TemperaturMaaling m;
      TEST_ASSERT_TRUE(rx->ny_maaling(m));
      TEST_ASSERT_EQUAL_INT16(-42, m.temperatur);  // -4,2 °C
    }
  }
}

void test_forstyrret_pakke_giver_ingen_forkert_maaling() {
  auto rx = std::make_unique<TemperaturModtager>();
  Signal s;
  s.add(500, false);
  s.pakke(temperatur_pakke(215, 0, 0, false));
  s.add(400, false);
  s.pakke(temperatur_pakke(215, 0, 0, false));
  s.add(400, false);
  // Tredje pakke ændres: én lang puls bliver kort.
  Signal p;
  p.pakke(temperatur_pakke(300, 0, 0, false));
  for (auto &st : p.stykker)
    if (st.second && st.first > 60) {
      st.first = 41;
      break;
    }
  for (auto &st : p.stykker) s.add(st.first, st.second);
  s.add(400, false);
  foed(*rx, s, 3.0);
  TemperaturMaaling m;
  TEST_ASSERT_EQUAL_UINT32(1, rx->gyldige());
  TEST_ASSERT_TRUE(rx->ny_maaling(m));
  TEST_ASSERT_EQUAL_INT16(215, m.temperatur);  // aldrig 30,0
  TEST_ASSERT_TRUE(rx->fejl() >= 1);
}

void test_tilfaeldig_stoej_giver_ingen_maalinger() {
  // 200.000 tilfældige bit (ca. 7 timers signal) med gyldige pulslængder:
  // intet må blive til en måling.
  std::mt19937 rng(11);
  auto rx = std::make_unique<TemperaturModtager>();
  uint32_t t = 0;
  rx->kant(t, false);
  for (int i = 0; i < 200000; ++i) {
    const bool b = rng() & 1;
    t += b ? 41 : 81;
    rx->kant(t, true);
    t += b ? 81 : 41;
    rx->kant(t, false);
  }
  char buf[64];
  snprintf(buf, sizeof(buf), "%u falske målinger", (unsigned)rx->gyldige());
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, rx->gyldige(), buf);
}

void test_konstant_indgang_giver_intet() {
  auto rx = std::make_unique<TemperaturModtager>();
  for (uint32_t t = 0; t < 600000; t += 19) rx->kant(t, false);
  rx->kant(600000, true);
  rx->kant(900000, false);
  TemperaturMaaling m;
  TEST_ASSERT_FALSE(rx->ny_maaling(m));
  (void)alle;
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_kontrolsum_eksempel);
  RUN_TEST(test_pakke_frem_og_tilbage);
  RUN_TEST(test_forkert_bit_afvises);
  RUN_TEST(test_pakker_med_pause);
  RUN_TEST(test_pakker_uden_pause_og_start_midt_i);
  RUN_TEST(test_foelerens_takt_kan_afvige);
  RUN_TEST(test_forstyrret_pakke_giver_ingen_forkert_maaling);
  RUN_TEST(test_tilfaeldig_stoej_giver_ingen_maalinger);
  RUN_TEST(test_konstant_indgang_giver_intet);
  return UNITY_END();
}
