// Unit-tests af IHC Sender og Modtager. Kører på PC: pio test -e native
#include <unity.h>

#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include "ihc_protocol.h"
#include "ihc_modtager.h"
#include "ihc_sender.h"
#include "../helpers/ihc_test_helpers.h"

using namespace ihc;
using namespace ihc_test;

// ---------------------------------------------------------------------------
// Paritet
// ---------------------------------------------------------------------------

void test_parity() {
  TEST_ASSERT_EQUAL_UINT32(1, parity_bit(0x0000));  // 0 ettaller = lige
  TEST_ASSERT_EQUAL_UINT32(0, parity_bit(0x0001));
  TEST_ASSERT_EQUAL_UINT32(1, parity_bit(0x0003));
  TEST_ASSERT_EQUAL_UINT32(1, parity_bit(0xFFFF));
  TEST_ASSERT_EQUAL_UINT32(0, parity_bit(0x7FFF));
  TEST_ASSERT_EQUAL_UINT32(16, count_ones(0xFFFF));
}

// ---------------------------------------------------------------------------
// Sender
// ---------------------------------------------------------------------------

void test_sender_frame_timing() {
  auto tx = std::make_unique<Sender>();
  tx->set_line_data(0, 0x0001);  // linje 0: kanal 1 ON
  tx->set_line_data(1, 0x0000);  // linje 1: alt OFF
  const auto f = one_frame(*tx);

  TEST_ASSERT_EQUAL_UINT32(122, f.size());
  for (uint32_t t = 0; t < kPauseTicks; ++t) TEST_ASSERT_EQUAL_HEX16(0x0000, f[t]);
  for (uint32_t t = kPauseTicks; t < kPauseTicks + kStartTicks; ++t) TEST_ASSERT_EQUAL_HEX16(0xFFFF, f[t]);

  // Bit 1: lav, lav, høj, og så lav for ON (linje 0) / høj for OFF (linje 1).
  const uint32_t b0 = kPauseTicks + kStartTicks;
  TEST_ASSERT_EQUAL_HEX16(0x0000, f[b0 + 0]);
  TEST_ASSERT_EQUAL_HEX16(0x0000, f[b0 + 1]);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, f[b0 + 2]);
  TEST_ASSERT_FALSE(f[b0 + 3] & 0x1);
  TEST_ASSERT_TRUE(f[b0 + 3] & 0x2);

  // Paritet (sidste tick): linje 0 har én 1-bit (ulige) -> paritet 0 -> lang høj.
  // Linje 1 har nul 1-bit (lige) -> paritet 1 -> kort høj.
  TEST_ASSERT_TRUE(f[121] & 0x1);
  TEST_ASSERT_FALSE(f[121] & 0x2);
}

void test_sender_lines_are_independent() {
  auto tx = std::make_unique<Sender>();
  for (uint32_t line = 0; line < 16; ++line) tx->set_line_data(line, static_cast<uint16_t>(0x00A5 ^ (line * 37)));
  const auto f = one_frame(*tx);
  for (uint32_t line = 0; line < 16; ++line) {
    const uint16_t data = static_cast<uint16_t>(0x00A5 ^ (line * 37));
    TEST_ASSERT_EQUAL_HEX32(data | (parity_bit(data) << 16), decode_ticks(f, line));
  }
}

void test_sender_snapshot_before_bits() {
  auto tx = std::make_unique<Sender>();
  tx->set_line_data(0, 0x0001);
  std::vector<uint16_t> f;
  for (uint32_t t = 0; t < kFrameTicks; ++t) {
    if (t == 40) tx->set_line_data(0, 0x00FF);  // under startpulsen: kommer med i denne frame
    if (t == 70) tx->set_line_data(0, 0x0000);  // midt i databittene: venter til næste frame
    f.push_back(tx->next_tick());
  }
  TEST_ASSERT_EQUAL_HEX32(0x00FF | (parity_bit(0xFF) << 16), decode_ticks(f, 0));
  TEST_ASSERT_EQUAL_HEX32(0x0000 | (1u << 16), decode_ticks(one_frame(*tx), 0));
  TEST_ASSERT_EQUAL_UINT32(2, tx->frames_sent());
}

// ---------------------------------------------------------------------------
// Modtager
// ---------------------------------------------------------------------------

void test_loopback_all_lines() {
  auto tx = std::make_unique<Sender>();
  auto rx = std::make_unique<Modtager>();
  const uint16_t values[8] = {0x0000, 0xFFFF, 0x8001, 0x1234, 0xABCD, 0x00FF, 0xFF00, 0x5555};
  for (uint32_t i = 0; i < 8; ++i) tx->set_line_data(i, values[i]);
  loopback(*tx, *rx, kFrameTicks * 3);
  for (uint32_t i = 0; i < 8; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(rx->online(i), "linje skal være online");
    TEST_ASSERT_EQUAL_HEX16(values[i], rx->state(i));
    TEST_ASSERT_EQUAL_UINT32(0, rx->stats(i).framing_errors);
  }
}

void test_modtager_needs_two_equal_frames() {
  auto rx = std::make_unique<Modtager>();
  Wave w;
  w.add_frame(0x0042);
  w.add(1000, 0);
  feed(*rx, {w});
  TEST_ASSERT_EQUAL_UINT32(1, rx->stats(0).frames_ok);
  TEST_ASSERT_FALSE(rx->online(0));

  Wave w2;
  w2.add_frame(0x0042);
  w2.add(1000, 0);
  feed(*rx, {w2});
  TEST_ASSERT_TRUE(rx->online(0));
  TEST_ASSERT_EQUAL_HEX16(0x0042, rx->state(0));

  // Ny værdi slår først igennem, når den er set to gange.
  Wave w3;
  w3.add_frame(0x0043);
  w3.add(1000, 0);
  feed(*rx, {w3});
  TEST_ASSERT_EQUAL_HEX16(0x0042, rx->state(0));
  feed(*rx, {w3});
  TEST_ASSERT_EQUAL_HEX16(0x0043, rx->state(0));
}

void test_modtager_timing_tolerance() {
  // Moduler med ±8 % afvigende takt og tilfældig sample-fase.
  const double scales[] = {0.92, 0.96, 1.0, 1.04, 1.08};
  std::mt19937 rng(1234);
  for (double scale : scales) {
    for (int rep = 0; rep < 5; ++rep) {
      auto rx = std::make_unique<Modtager>();
      const uint16_t data = static_cast<uint16_t>(rng());
      Wave w;
      for (int i = 0; i < 3; ++i) w.add_frame(data, scale);
      w.add(1000, 0);
      feed(*rx, {w}, rng() % (kSampleUs * 1000));
      TEST_ASSERT_TRUE_MESSAGE(rx->online(0), "frame skal accepteres inden for tolerancen");
      TEST_ASSERT_EQUAL_HEX16(data, rx->state(0));
      TEST_ASSERT_EQUAL_UINT32(0, rx->stats(0).framing_errors);
    }
  }
}

void test_modtager_rejects_out_of_tolerance() {
  // 30 % for hurtigt: kort/lang puls kan ikke skelnes sikkert -> afvises.
  auto rx = std::make_unique<Modtager>();
  Wave w;
  for (int i = 0; i < 3; ++i) w.add_frame(0x00FF, 0.6);
  feed(*rx, {w});
  TEST_ASSERT_FALSE(rx->online(0));
  TEST_ASSERT_EQUAL_UINT32(0, rx->stats(0).frames_ok);
}

void test_modtager_parity_detection() {
  // 0x0100: én 1-bit i alt (ulige) men nul 1-bit i bit 1-8 (lige).
  // Paritet efter "16-bit"-reglen = 0, efter "8-bit"-reglen = 1.
  const uint16_t data = 0x0100;
  const uint32_t p16 = parity_bit(data);
  const uint32_t p8 = parity_bit(data & 0xFF);
  TEST_ASSERT_NOT_EQUAL(p16, p8);

  // Auto: modulet bruger 8-bit-reglen -> accepteres, og statistikken viser det.
  auto rx = std::make_unique<Modtager>();
  Wave w;
  for (int i = 0; i < 3; ++i) w.add_frame_parity(data, p8);
  feed(*rx, {w});
  TEST_ASSERT_TRUE(rx->online(0));
  TEST_ASSERT_EQUAL_UINT32(3, rx->stats(0).parity8_match);
  TEST_ASSERT_EQUAL_UINT32(0, rx->stats(0).parity16_match);

  // Låst til 16-bit-reglen -> samme frames afvises.
  auto rx16 = std::make_unique<Modtager>();
  rx16->set_parity_mode(ParityMode::kAll16);
  feed(*rx16, {w});
  TEST_ASSERT_FALSE(rx16->online(0));
  TEST_ASSERT_EQUAL_UINT32(3, rx16->stats(0).parity_errors);

  // Forkert paritet efter begge regler -> afvises også i Auto.
  auto rxa = std::make_unique<Modtager>();
  Wave bad;
  for (int i = 0; i < 3; ++i) bad.add_frame_parity(0x0003, 0);  // to 1-bit: begge regler kræver 1
  bad.add(1000, 0);  // afsluttende lav, så sidste frame afsluttes
  feed(*rxa, {bad});
  TEST_ASSERT_FALSE(rxa->online(0));
  TEST_ASSERT_EQUAL_UINT32(3, rxa->stats(0).parity_errors);
}

void test_modtager_glitch_drops_only_that_frame() {
  auto rx = std::make_unique<Modtager>();
  Wave good;
  good.add_frame(0x0F0F);
  good.add_frame(0x0F0F);
  feed(*rx, {good});
  TEST_ASSERT_TRUE(rx->online(0));

  // Frame med en 40 µs støjspids midt i en lang høj puls.
  Wave glitch;
  glitch.add(4000, 0);
  glitch.add(4370, 1);
  glitch.add(312, 0);
  glitch.add(120, 1);
  glitch.add(40, 0);  // spids
  glitch.add(152, 1);
  feed(*rx, {glitch});
  feed_idle(*rx, 1000);
  TEST_ASSERT_EQUAL_UINT32(1, rx->stats(0).framing_errors);
  TEST_ASSERT_EQUAL_HEX16(0x0F0F, rx->state(0));
  TEST_ASSERT_TRUE(rx->online(0));
}

void test_modtager_offline_and_reconnect() {
  auto rx = std::make_unique<Modtager>();
  Wave w;
  w.add_frame(0x1111);
  w.add_frame(0x1111);
  feed(*rx, {w});
  TEST_ASSERT_TRUE(rx->online(0));

  feed_idle(*rx, 250000);  // 250 ms uden frames (ledning faldet af)
  TEST_ASSERT_FALSE(rx->online(0));

  // Efter udfald kræves igen to ens frames, og den gamle værdi vises ikke.
  Wave again;
  again.add_frame(0x2222);
  feed(*rx, {again});
  TEST_ASSERT_FALSE(rx->online(0));
  feed(*rx, {again});
  TEST_ASSERT_TRUE(rx->online(0));
  TEST_ASSERT_EQUAL_HEX16(0x2222, rx->state(0));
}

void test_modtager_stuck_high_line() {
  auto rx = std::make_unique<Modtager>();
  feed_idle(*rx, 100000, 0x01);  // linje 0 hænger høj
  feed_idle(*rx, 1000, 0x00);
  TEST_ASSERT_FALSE(rx->online(0));
  TEST_ASSERT_EQUAL_UINT32(0, rx->stats(0).frames_ok);
}

void test_modtager_inverted_input() {
  auto rx = std::make_unique<Modtager>();
  rx->set_inverted(true);
  Wave w;
  for (int i = 0; i < 2; ++i) w.add_frame(0xC3C3);
  w.add(1000, 0);
  // Vend signalet, som en optokobler ville.
  for (auto &s : w.segs) s.level ^= 1;
  feed(*rx, {w});
  TEST_ASSERT_TRUE(rx->online(0));
  TEST_ASSERT_EQUAL_HEX16(0xC3C3, rx->state(0));
}

void test_modtager_lines_are_independent() {
  // Linje 0 sender gyldige frames, linje 1 sender støj, linje 2 er død.
  auto rx = std::make_unique<Modtager>();
  Wave ok, noise;
  for (int i = 0; i < 3; ++i) ok.add_frame(0xBEEF);
  std::mt19937 rng(7);
  while (noise.total_ns < ok.total_ns) noise.add(20 + rng() % 400, rng() & 1);
  feed(*rx, {ok, noise, Wave{}});
  TEST_ASSERT_TRUE(rx->online(0));
  TEST_ASSERT_EQUAL_HEX16(0xBEEF, rx->state(0));
  TEST_ASSERT_FALSE(rx->online(1));
  TEST_ASSERT_FALSE(rx->online(2));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parity);
  RUN_TEST(test_sender_frame_timing);
  RUN_TEST(test_sender_lines_are_independent);
  RUN_TEST(test_sender_snapshot_before_bits);
  RUN_TEST(test_loopback_all_lines);
  RUN_TEST(test_modtager_needs_two_equal_frames);
  RUN_TEST(test_modtager_timing_tolerance);
  RUN_TEST(test_modtager_rejects_out_of_tolerance);
  RUN_TEST(test_modtager_parity_detection);
  RUN_TEST(test_modtager_glitch_drops_only_that_frame);
  RUN_TEST(test_modtager_offline_and_reconnect);
  RUN_TEST(test_modtager_stuck_high_line);
  RUN_TEST(test_modtager_inverted_input);
  RUN_TEST(test_modtager_lines_are_independent);
  return UNITY_END();
}
