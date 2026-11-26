// Unit-tests af sessioner og login-bremse. Kører på PC: pio test -e native
#include <unity.h>

#include <cstring>
#include <memory>
#include <string>

#include "sessioner.h"

using namespace netvaerk;

constexpr int64_t S = 1000;
constexpr int64_t DAG = 24 * 60 * 60 * S;

static void tilfaeldig(uint8_t buf[32], uint8_t seed) {
  for (int i = 0; i < 32; ++i) buf[i] = static_cast<uint8_t>(seed * 31 + i * 7);
}

void test_session_opret_og_tjek() {
  auto s = std::make_unique<Sessioner>();
  uint8_t r[32];
  tilfaeldig(r, 1);
  const std::string t = s->opret(0, r);
  TEST_ASSERT_EQUAL_UINT32(64, t.size());
  TEST_ASSERT_TRUE(s->gyldig(t, 1000));
  TEST_ASSERT_FALSE(s->gyldig("forkert", 1000));
  std::string forkert = t;
  forkert[10] = forkert[10] == 'a' ? 'b' : 'a';
  TEST_ASSERT_FALSE(s->gyldig(forkert, 1000));
}

void test_session_udloeber_efter_7_dages_inaktivitet() {
  auto s = std::make_unique<Sessioner>();
  uint8_t r[32];
  tilfaeldig(r, 2);
  const std::string t = s->opret(0, r);
  // Brugt hver 6. dag: forbliver gyldig.
  TEST_ASSERT_TRUE(s->gyldig(t, 6 * DAG));
  TEST_ASSERT_TRUE(s->gyldig(t, 12 * DAG));
  // 8 dage uden brug: udløbet.
  TEST_ASSERT_FALSE(s->gyldig(t, 20 * DAG + 1));
}

void test_session_log_ud() {
  auto s = std::make_unique<Sessioner>();
  uint8_t r1[32], r2[32];
  tilfaeldig(r1, 3);
  tilfaeldig(r2, 4);
  const std::string a = s->opret(0, r1);
  const std::string b = s->opret(0, r2);
  s->slet(a);
  TEST_ASSERT_FALSE(s->gyldig(a, 1));
  TEST_ASSERT_TRUE(s->gyldig(b, 1));
  s->slet_alle();
  TEST_ASSERT_FALSE(s->gyldig(b, 2));
  TEST_ASSERT_EQUAL_INT(0, s->antal(2));
}

void test_session_fuld_erstatter_den_aeldste() {
  auto s = std::make_unique<Sessioner>();
  std::string tokens[Sessioner::kMax];
  for (int i = 0; i < Sessioner::kMax; ++i) {
    uint8_t r[32];
    tilfaeldig(r, static_cast<uint8_t>(10 + i));
    tokens[i] = s->opret(i * S, r);
  }
  // Session 0 bruges igen; session 1 er nu den ældste.
  TEST_ASSERT_TRUE(s->gyldig(tokens[0], 100 * S));
  uint8_t r[32];
  tilfaeldig(r, 99);
  const std::string ny = s->opret(200 * S, r);
  TEST_ASSERT_TRUE(s->gyldig(ny, 201 * S));
  TEST_ASSERT_TRUE(s->gyldig(tokens[0], 201 * S));
  TEST_ASSERT_FALSE(s->gyldig(tokens[1], 201 * S));
}

void test_login_bremse() {
  LoginBremse b;
  for (int i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_INT64(0, b.ventetid_ms(i * S));
    b.fejl(i * S);
  }
  TEST_ASSERT_EQUAL_INT64(0, b.ventetid_ms(4 * S));
  b.fejl(4 * S);  // 5. fejl: 30 s pause
  TEST_ASSERT_EQUAL_INT64(30 * S, b.ventetid_ms(4 * S));
  TEST_ASSERT_EQUAL_INT64(0, b.ventetid_ms(34 * S));
  b.fejl(34 * S);  // 6. fejl: 60 s
  TEST_ASSERT_EQUAL_INT64(60 * S, b.ventetid_ms(34 * S));
  for (int i = 0; i < 10; ++i) b.fejl(1000 * S);
  TEST_ASSERT_EQUAL_INT64(LoginBremse::kMaxVentMs, b.ventetid_ms(1000 * S));  // højst 15 min
  b.succes();
  TEST_ASSERT_EQUAL_INT64(0, b.ventetid_ms(1000 * S));
}

void test_konstant_tid_sammenligning() {
  const uint8_t a[] = {1, 2, 3, 4};
  const uint8_t b[] = {1, 2, 3, 4};
  const uint8_t c[] = {1, 2, 3, 5};
  TEST_ASSERT_TRUE(ens_konstant_tid(a, b, 4));
  TEST_ASSERT_FALSE(ens_konstant_tid(a, c, 4));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_session_opret_og_tjek);
  RUN_TEST(test_session_udloeber_efter_7_dages_inaktivitet);
  RUN_TEST(test_session_log_ud);
  RUN_TEST(test_session_fuld_erstatter_den_aeldste);
  RUN_TEST(test_login_bremse);
  RUN_TEST(test_konstant_tid_sammenligning);
  return UNITY_END();
}
