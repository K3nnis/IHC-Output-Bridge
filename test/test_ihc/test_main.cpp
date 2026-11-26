// Unit-tests af IHC Sender og Receiver. Kører på PC: pio test -e native
#include <unity.h>

#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include "ihc_protocol.h"
#include "ihc_receiver.h"
#include "ihc_sender.h"

using namespace ihc;

// ---------------------------------------------------------------------------
// Hjælpere
// ---------------------------------------------------------------------------

// Bølgeform for én linje som en række (varighed i ns, niveau).
struct Wave {
  struct Seg {
    uint64_t end_ns;
    uint8_t level;
  };
  std::vector<Seg> segs;
  uint64_t total_ns = 0;

  void add(double us, uint8_t level) {
    total_ns += static_cast<uint64_t>(us * 1000.0);
    segs.push_back({total_ns, level});
  }

  // Én IHC-frame som et inputmodul ville sende den. scale > 1 = langsommere modul.
  void add_frame_parity(uint16_t data, uint32_t parity, double scale = 1.0) {
    add(4000 * scale, 0);  // pause
    add(4370 * scale, 1);  // startpuls
    const uint32_t frame = data | (parity << 16);
    for (uint32_t b = 0; b < 17; ++b) {
      const bool on = frame & (1u << b);
      add(312 * scale, 0);
      if (on) {
        add(156 * scale, 1);
        add(156 * scale, 0);
      } else {
        add(312 * scale, 1);
      }
    }
  }
  void add_frame(uint16_t data, double scale = 1.0) { add_frame_parity(data, parity_bit(data), scale); }

  uint8_t level_at(uint64_t t_ns) const {
    for (const auto &s : segs)
      if (t_ns < s.end_ns) return s.level;
    return 0;  // efter sidste segment: linjen er lav
  }
};

// Sampler bølgeformerne hvert 39 µs (startfase offset_ns) og fodrer Receiver.
static void feed(Receiver &rx, const std::vector<Wave> &waves, uint64_t offset_ns = 0) {
  uint64_t total = 0;
  for (const auto &w : waves) total = w.total_ns > total ? w.total_ns : total;
  for (uint64_t t = offset_ns; t < total; t += kSampleUs * 1000) {
    uint8_t levels = 0;
    for (size_t i = 0; i < waves.size(); ++i) levels |= waves[i].level_at(t) << i;
    rx.sample(levels);
  }
}

static void feed_idle(Receiver &rx, uint32_t us, uint8_t levels = 0) {
  for (uint32_t i = 0; i < us / kSampleUs; ++i) rx.sample(levels);
}

// Kører Sender i et antal ticks og sender linje 0-7 videre til Receiver.
static void loopback(Sender &tx, Receiver &rx, uint32_t ticks) {
  for (uint32_t t = 0; t < ticks; ++t) {
    const uint8_t levels = static_cast<uint8_t>(tx.next_tick() & 0xFF);
    for (uint32_t s = 0; s < kSamplesPerTick; ++s) rx.sample(levels);
  }
}

// Decoder én linje direkte fra Senders tick-ord (uafhængig af Receiver).
static uint32_t decode_ticks(const std::vector<uint16_t> &ticks, uint32_t line) {
  uint32_t frame = 0;
  for (uint32_t b = 0; b < kFrameBits; ++b) {
    const uint32_t base = kPauseTicks + kStartTicks + b * kTicksPerBit;
    const bool short_high = !(ticks[base + 3] & (1u << line));
    if (short_high) frame |= 1u << b;
  }
  return frame;
}

static std::vector<uint16_t> one_frame(Sender &tx) {
  std::vector<uint16_t> v;
  for (uint32_t t = 0; t < kFrameTicks; ++t) v.push_back(tx.next_tick());
  return v;
}

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

void test_sender_snapshot_at_frame_start() {
  auto tx = std::make_unique<Sender>();
  tx->set_line_data(0, 0x00FF);
  std::vector<uint16_t> f;
  for (uint32_t t = 0; t < kFrameTicks; ++t) {
    if (t == 70) tx->set_line_data(0, 0x0000);  // ændring midt i frame
    f.push_back(tx->next_tick());
  }
  TEST_ASSERT_EQUAL_HEX32(0x00FF | (parity_bit(0xFF) << 16), decode_ticks(f, 0));
  TEST_ASSERT_EQUAL_HEX32(0x0000 | (1u << 16), decode_ticks(one_frame(*tx), 0));
  TEST_ASSERT_EQUAL_UINT32(2, tx->frames_sent());
}

// ---------------------------------------------------------------------------
// Receiver
// ---------------------------------------------------------------------------

void test_loopback_all_lines() {
  auto tx = std::make_unique<Sender>();
  auto rx = std::make_unique<Receiver>();
  const uint16_t values[8] = {0x0000, 0xFFFF, 0x8001, 0x1234, 0xABCD, 0x00FF, 0xFF00, 0x5555};
  for (uint32_t i = 0; i < 8; ++i) tx->set_line_data(i, values[i]);
  loopback(*tx, *rx, kFrameTicks * 3);
  for (uint32_t i = 0; i < 8; ++i) {
    TEST_ASSERT_TRUE_MESSAGE(rx->online(i), "linje skal være online");
    TEST_ASSERT_EQUAL_HEX16(values[i], rx->state(i));
    TEST_ASSERT_EQUAL_UINT32(0, rx->stats(i).framing_errors);
  }
}

void test_receiver_needs_two_equal_frames() {
  auto rx = std::make_unique<Receiver>();
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

void test_receiver_timing_tolerance() {
  // Moduler med ±8 % afvigende takt og tilfældig sample-fase.
  const double scales[] = {0.92, 0.96, 1.0, 1.04, 1.08};
  std::mt19937 rng(1234);
  for (double scale : scales) {
    for (int rep = 0; rep < 5; ++rep) {
      auto rx = std::make_unique<Receiver>();
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

void test_receiver_rejects_out_of_tolerance() {
  // 30 % for hurtigt: kort/lang puls kan ikke skelnes sikkert -> afvises.
  auto rx = std::make_unique<Receiver>();
  Wave w;
  for (int i = 0; i < 3; ++i) w.add_frame(0x00FF, 0.6);
  feed(*rx, {w});
  TEST_ASSERT_FALSE(rx->online(0));
  TEST_ASSERT_EQUAL_UINT32(0, rx->stats(0).frames_ok);
}

void test_receiver_parity_detection() {
  // 0x0100: én 1-bit i alt (ulige) men nul 1-bit i bit 1-8 (lige).
  // Paritet efter "16-bit"-reglen = 0, efter "8-bit"-reglen = 1.
  const uint16_t data = 0x0100;
  const uint32_t p16 = parity_bit(data);
  const uint32_t p8 = parity_bit(data & 0xFF);
  TEST_ASSERT_NOT_EQUAL(p16, p8);

  // Auto: modulet bruger 8-bit-reglen -> accepteres, og statistikken viser det.
  auto rx = std::make_unique<Receiver>();
  Wave w;
  for (int i = 0; i < 3; ++i) w.add_frame_parity(data, p8);
  feed(*rx, {w});
  TEST_ASSERT_TRUE(rx->online(0));
  TEST_ASSERT_EQUAL_UINT32(3, rx->stats(0).parity8_match);
  TEST_ASSERT_EQUAL_UINT32(0, rx->stats(0).parity16_match);

  // Låst til 16-bit-reglen -> samme frames afvises.
  auto rx16 = std::make_unique<Receiver>();
  rx16->set_parity_mode(ParityMode::kAll16);
  feed(*rx16, {w});
  TEST_ASSERT_FALSE(rx16->online(0));
  TEST_ASSERT_EQUAL_UINT32(3, rx16->stats(0).parity_errors);

  // Forkert paritet efter begge regler -> afvises også i Auto.
  auto rxa = std::make_unique<Receiver>();
  Wave bad;
  for (int i = 0; i < 3; ++i) bad.add_frame_parity(0x0003, 0);  // to 1-bit: begge regler kræver 1
  bad.add(1000, 0);  // afsluttende lav, så sidste frame afsluttes
  feed(*rxa, {bad});
  TEST_ASSERT_FALSE(rxa->online(0));
  TEST_ASSERT_EQUAL_UINT32(3, rxa->stats(0).parity_errors);
}

void test_receiver_glitch_drops_only_that_frame() {
  auto rx = std::make_unique<Receiver>();
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

void test_receiver_offline_and_reconnect() {
  auto rx = std::make_unique<Receiver>();
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

void test_receiver_stuck_high_line() {
  auto rx = std::make_unique<Receiver>();
  feed_idle(*rx, 100000, 0x01);  // linje 0 hænger høj
  feed_idle(*rx, 1000, 0x00);
  TEST_ASSERT_FALSE(rx->online(0));
  TEST_ASSERT_EQUAL_UINT32(0, rx->stats(0).frames_ok);
}

void test_receiver_inverted_input() {
  auto rx = std::make_unique<Receiver>();
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

void test_receiver_lines_are_independent() {
  // Linje 0 sender gyldige frames, linje 1 sender støj, linje 2 er død.
  auto rx = std::make_unique<Receiver>();
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
  RUN_TEST(test_sender_snapshot_at_frame_start);
  RUN_TEST(test_loopback_all_lines);
  RUN_TEST(test_receiver_needs_two_equal_frames);
  RUN_TEST(test_receiver_timing_tolerance);
  RUN_TEST(test_receiver_rejects_out_of_tolerance);
  RUN_TEST(test_receiver_parity_detection);
  RUN_TEST(test_receiver_glitch_drops_only_that_frame);
  RUN_TEST(test_receiver_offline_and_reconnect);
  RUN_TEST(test_receiver_stuck_high_line);
  RUN_TEST(test_receiver_inverted_input);
  RUN_TEST(test_receiver_lines_are_independent);
  return UNITY_END();
}
