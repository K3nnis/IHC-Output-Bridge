// Unit-tests af sammenligning af versionsnumre. Kører på PC: pio test -e native
#include <unity.h>

#include "versioner.h"

using namespace netvaerk;

void test_raekkefoelge() {
  // Fra ældst til nyest.
  const char *v[] = {"1.9.9", "2.0.0-dev", "2.0.0-alpha", "2.0.0-beta.1", "2.0.0-beta.2",
                     "2.0.0-beta.10", "2.0.0-rc.1", "2.0.0", "2.0.1", "2.1.0", "10.0.0"};
  const int n = sizeof(v) / sizeof(v[0]);
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      const int r = sammenlign_versioner(v[i], v[j]);
      if (i < j) TEST_ASSERT_TRUE_MESSAGE(r < 0, v[i]);
      if (i == j) TEST_ASSERT_EQUAL_INT(0, r);
      if (i > j) TEST_ASSERT_TRUE_MESSAGE(r > 0, v[i]);
    }
  }
}

void test_v_foran_og_byggedata() {
  TEST_ASSERT_EQUAL_INT(0, sammenlign_versioner("v2.0.0", "2.0.0"));
  TEST_ASSERT_EQUAL_INT(0, sammenlign_versioner("2.0.0+build.5", "2.0.0"));
  TEST_ASSERT_EQUAL_INT(0, sammenlign_versioner("2.0", "2.0.0"));
}

void test_er_nyere() {
  TEST_ASSERT_TRUE(er_nyere("v2.0.0-beta.2", "2.0.0-beta.1"));
  TEST_ASSERT_TRUE(er_nyere("2.0.0-beta.1", "2.0.0-dev"));  // udviklerbuild tilbydes udgivne versioner
  TEST_ASSERT_TRUE(er_nyere("2.0.0", "2.0.0-beta.2"));
  TEST_ASSERT_FALSE(er_nyere("2.0.0-beta.1", "2.0.0"));
  TEST_ASSERT_FALSE(er_nyere("2.0.0", "2.0.0"));
  TEST_ASSERT_FALSE(er_nyere("1.9.0", "2.0.0-dev"));
}

void test_testversion() {
  TEST_ASSERT_TRUE(er_testversion("2.0.0-beta.1"));
  TEST_ASSERT_FALSE(er_testversion("2.0.0"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_raekkefoelge);
  RUN_TEST(test_v_foran_og_byggedata);
  RUN_TEST(test_er_nyere);
  RUN_TEST(test_testversion);
  return UNITY_END();
}
