// Unit-tests af regler, Styring og reaktionstid fra tryk til udgang.
// Kører på PC: pio test -e native
#include <unity.h>

#include <cstdio>
#include <memory>
#include <random>
#include <vector>

#include "../helpers/ihc_test_helpers.h"
#include "ihc_modtager.h"
#include "ihc_regler.h"
#include "ihc_sender.h"
#include "ihc_styring.h"

using namespace ihc;
using namespace ihc_test;

// Regel med numre som i UI'et (modul og kanal fra 1).
static Regel kip(uint8_t in_mod, uint8_t in_ch, uint8_t out_mod, uint8_t out_ch) {
  return Regel{RegelType::kKip, static_cast<uint8_t>(in_mod - 1), static_cast<uint8_t>(in_ch - 1),
               static_cast<uint8_t>(out_mod - 1), static_cast<uint8_t>(out_ch - 1)};
}

static uint8_t bit(uint8_t out_ch) { return static_cast<uint8_t>(1u << (out_ch - 1)); }

// Hele kæden som i firmwaren: Modtager -> Styring (regler) -> Sender.
struct Kaede {
  Sender tx;
  Modtager rx;
  Regeltabel tabel;
  Styring styring{tx};

  explicit Kaede(const std::vector<Regel> &regler) {
    tabel.build(regler.data(), regler.size());
    styring.set_regeltabel(&tabel);
    rx.set_change_handler(
        [](void *ctx, uint32_t line, uint16_t old_state, uint16_t new_state, bool had_state) {
          static_cast<Kaede *>(ctx)->styring.on_input_change(line, old_state, new_state, had_state);
        },
        this);
  }
};

// ---------------------------------------------------------------------------
// Regeltabel
// ---------------------------------------------------------------------------

void test_regel_validering() {
  TEST_ASSERT_TRUE(regel_gyldig(kip(1, 1, 1, 1)));
  TEST_ASSERT_TRUE(regel_gyldig(kip(8, 16, 16, 8)));
  TEST_ASSERT_FALSE(regel_gyldig(kip(9, 1, 1, 1)));   // kun 8 inputmoduler
  TEST_ASSERT_FALSE(regel_gyldig(kip(1, 17, 1, 1)));  // kun 16 indgange
  TEST_ASSERT_FALSE(regel_gyldig(kip(1, 1, 17, 1)));  // kun 16 outputmoduler
  TEST_ASSERT_FALSE(regel_gyldig(kip(1, 1, 1, 9)));   // kun 8 udgange
}

void test_tabel_kipper_rigtige_udgange() {
  auto t = std::make_unique<Regeltabel>();
  const std::vector<Regel> r = {kip(1, 8, 2, 2), kip(1, 8, 5, 1), kip(3, 1, 2, 3)};
  TEST_ASSERT_EQUAL_UINT32(3, t->build(r.data(), r.size()));

  uint8_t out[kOutputModules] = {};
  // Indgang 1.8 trykkes: udgang 2.2 og 5.1 kippes.
  const uint16_t changed = t->apply(0, 1u << 7, out);
  TEST_ASSERT_EQUAL_HEX16((1u << 1) | (1u << 4), changed);
  TEST_ASSERT_EQUAL_HEX8(bit(2), out[1]);
  TEST_ASSERT_EQUAL_HEX8(bit(1), out[4]);
  // Igen: slukkes.
  t->apply(0, 1u << 7, out);
  TEST_ASSERT_EQUAL_HEX8(0, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0, out[4]);
  // Indgang uden regel ændrer intet.
  TEST_ASSERT_EQUAL_HEX16(0, t->apply(0, 1u << 0, out));
}

void test_tabel_dublet_kipper_kun_en_gang() {
  auto t = std::make_unique<Regeltabel>();
  const std::vector<Regel> r = {kip(1, 1, 1, 1), kip(1, 1, 1, 1)};
  t->build(r.data(), r.size());
  uint8_t out[kOutputModules] = {};
  t->apply(0, 1, out);
  TEST_ASSERT_EQUAL_HEX8(bit(1), out[0]);
}

void test_tabel_springer_ugyldige_over() {
  auto t = std::make_unique<Regeltabel>();
  const std::vector<Regel> r = {kip(1, 1, 1, 1), kip(9, 1, 1, 1)};
  TEST_ASSERT_EQUAL_UINT32(1, t->build(r.data(), r.size()));
}

void test_regler_gem_og_indlaes() {
  const std::vector<Regel> r = {kip(1, 8, 2, 2), kip(8, 16, 16, 8), kip(4, 3, 7, 5)};
  uint8_t buf[64];
  const size_t n = regler_til_bytes(r.data(), r.size(), buf, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT32(3 + 3 * 5, n);

  Regel ud[8];
  TEST_ASSERT_EQUAL_INT(3, regler_fra_bytes(buf, n, ud, 8));
  for (size_t i = 0; i < r.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT8(r[i].in_module, ud[i].in_module);
    TEST_ASSERT_EQUAL_UINT8(r[i].in_channel, ud[i].in_channel);
    TEST_ASSERT_EQUAL_UINT8(r[i].out_module, ud[i].out_module);
    TEST_ASSERT_EQUAL_UINT8(r[i].out_channel, ud[i].out_channel);
  }

  // Beskadigede data afvises.
  TEST_ASSERT_EQUAL_INT(-1, regler_fra_bytes(buf, n - 1, ud, 8));  // forkert længde
  buf[0] = 99;
  TEST_ASSERT_EQUAL_INT(-1, regler_fra_bytes(buf, n, ud, 8));  // ukendt version
  buf[0] = kReglerFormatVersion;
  buf[3 + 1] = 200;  // ugyldigt inputmodul
  TEST_ASSERT_EQUAL_INT(-1, regler_fra_bytes(buf, n, ud, 8));
  // For lille buffer.
  TEST_ASSERT_EQUAL_UINT32(0, regler_til_bytes(r.data(), r.size(), buf, 10));
}

// ---------------------------------------------------------------------------
// Styring
// ---------------------------------------------------------------------------

void test_styring_kip_paa_tryk_ikke_paa_slip() {
  auto k = std::make_unique<Kaede>(std::vector<Regel>{kip(1, 8, 2, 2)});
  Styring &s = k->styring;
  // Tryk (OFF -> ON): tænder.
  s.on_input_change(0, 0x0000, 0x0080, true);
  TEST_ASSERT_EQUAL_HEX8(bit(2), s.output_module(1));
  TEST_ASSERT_EQUAL_HEX16(bit(2), k->tx.line_data(1));
  // Slip (ON -> OFF): ingen ændring.
  s.on_input_change(0, 0x0080, 0x0000, true);
  TEST_ASSERT_EQUAL_HEX8(bit(2), s.output_module(1));
  // Nyt tryk: slukker.
  s.on_input_change(0, 0x0000, 0x0080, true);
  TEST_ASSERT_EQUAL_HEX8(0, s.output_module(1));
}

void test_styring_ingen_reaktion_ved_genforbindelse() {
  auto k = std::make_unique<Kaede>(std::vector<Regel>{kip(1, 8, 2, 2)});
  // Første bekræftelse efter opstart/udfald: tilstand ukendt før -> ingen kip.
  TEST_ASSERT_EQUAL_HEX16(0, k->styring.on_input_change(0, 0x0000, 0x0080, false));
  TEST_ASSERT_EQUAL_HEX8(0, k->styring.output_module(1));
}

void test_styring_manuel_og_regel_arbejder_sammen() {
  auto k = std::make_unique<Kaede>(std::vector<Regel>{kip(1, 1, 1, 1)});
  Styring &s = k->styring;
  s.set_output(0, 0, true);  // tændt via MQTT/web
  s.on_input_change(0, 0, 1, true);  // tryk: kipper til slukket
  TEST_ASSERT_EQUAL_HEX8(0, s.output_module(0));
  TEST_ASSERT_EQUAL_HEX16(0, s.set_output(0, 0, false));  // allerede slukket: ingen ændring
  TEST_ASSERT_EQUAL_HEX16(1, s.toggle_output(0, 0));
  TEST_ASSERT_EQUAL_HEX8(bit(1), s.output_module(0));
}

void test_styring_indlaes_gemt_tilstand() {
  auto k = std::make_unique<Kaede>(std::vector<Regel>{});
  uint8_t gemt[kOutputModules] = {};
  gemt[0] = 0x81;
  gemt[15] = 0x42;
  k->styring.load(gemt);
  TEST_ASSERT_EQUAL_HEX16(0x81, k->tx.line_data(0));
  TEST_ASSERT_EQUAL_HEX16(0x42, k->tx.line_data(15));
  uint8_t kopi[kOutputModules];
  k->styring.copy_outputs(kopi);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(gemt, kopi, kOutputModules);
}

// ---------------------------------------------------------------------------
// Reaktionstid: tryk på indgang -> færdig frame med tændt udgang
// ---------------------------------------------------------------------------

struct LatencyResult {
  double max_ms = 0;
  double sum_ms = 0;
  int runs = 0;
};

// Simulerer hele kæden med 39 µs opløsning, præcis som interruptet kører.
// Inputmodulet antages at aflæse knappen ved starten af hver af sine frames
// (værste antagelse), og dets takt kan afvige (scale) og have vilkårlig fase.
static double simulate_press(double press_ms, double module_phase_us, double scale) {
  auto k = std::make_unique<Kaede>(std::vector<Regel>{kip(1, 8, 2, 2)});

  // Inputmodulets bølgeform: frames efter hinanden, knap trykket fra press_ms.
  Wave w;
  w.add(module_phase_us, 0);
  while (w.total_ns < static_cast<uint64_t>((press_ms + 400) * 1e6)) {
    const double frame_start_ms = w.total_ns / 1e6;
    const uint16_t data = frame_start_ms >= press_ms ? (1u << 7) : 0;  // indgang 8
    w.add_frame(data, scale);
  }

  std::vector<uint16_t> frame_ticks;
  uint64_t sample = 0;
  const uint64_t sample_ns = kSampleUs * 1000;
  for (uint64_t t = 0; t < w.total_ns; t += sample_ns, ++sample) {
    if (sample % kSamplesPerTick == 0) {
      frame_ticks.push_back(k->tx.next_tick());
      if (frame_ticks.size() == kFrameTicks) {
        const uint32_t frame = decode_ticks(frame_ticks, 1);  // outputmodul 2
        frame_ticks.clear();
        const double frame_end_ms = (t + kTickUs * 1000) / 1e6;
        if ((frame & bit(2)) && frame_end_ms > press_ms) return frame_end_ms - press_ms;
      }
    }
    k->rx.sample(w.level_at(t));
  }
  return -1;  // udgangen tændte aldrig
}

static LatencyResult run_latency(double scale) {
  LatencyResult r;
  std::mt19937 rng(42);
  std::uniform_real_distribution<double> press(150.0, 170.0);  // efter at modulet er online
  std::uniform_real_distribution<double> phase(0.0, 19000.0);
  for (int i = 0; i < 300; ++i) {
    const double ms = simulate_press(press(rng), phase(rng), scale);
    TEST_ASSERT_TRUE_MESSAGE(ms > 0, "udgangen tændte ikke");
    r.max_ms = ms > r.max_ms ? ms : r.max_ms;
    r.sum_ms += ms;
    ++r.runs;
  }
  return r;
}

void test_reaktionstid() {
  // Kravet er under 500 ms. Designmålet er under 100 ms i værste fald.
  const double scales[] = {0.95, 1.0, 1.05};
  for (double scale : scales) {
    const LatencyResult r = run_latency(scale);
    char msg[160];
    snprintf(msg, sizeof(msg), "Modultakt %.0f %%: gennemsnit %.1f ms, værst %.1f ms (%d tryk)", scale * 100,
             r.sum_ms / r.runs, r.max_ms, r.runs);
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE_MESSAGE(r.max_ms < 100.0, msg);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_regel_validering);
  RUN_TEST(test_tabel_kipper_rigtige_udgange);
  RUN_TEST(test_tabel_dublet_kipper_kun_en_gang);
  RUN_TEST(test_tabel_springer_ugyldige_over);
  RUN_TEST(test_regler_gem_og_indlaes);
  RUN_TEST(test_styring_kip_paa_tryk_ikke_paa_slip);
  RUN_TEST(test_styring_ingen_reaktion_ved_genforbindelse);
  RUN_TEST(test_styring_manuel_og_regel_arbejder_sammen);
  RUN_TEST(test_styring_indlaes_gemt_tilstand);
  RUN_TEST(test_reaktionstid);
  return UNITY_END();
}
