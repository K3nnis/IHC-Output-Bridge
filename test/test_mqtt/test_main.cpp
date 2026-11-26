// Unit-tests af MQTT-topics, kommandoer og HA-discovery.
// Kører på PC: pio test -e native
#include <unity.h>

#include <string>

#include "mqtt_topics.h"

using namespace netvaerk;

static const std::string B = "ihc/ihc-bridge";

void test_topics_som_i_eksemplet() {
  // Inputmodul 3, indgang 15.
  TEST_ASSERT_EQUAL_STRING("ihc/ihc-bridge/input/3/15/state", topic_indgang(B, 3, 15).c_str());
  TEST_ASSERT_EQUAL_STRING("ihc/ihc-bridge/output/2/2/state", topic_udgang(B, 2, 2).c_str());
  TEST_ASSERT_EQUAL_STRING("ihc/ihc-bridge/output/2/2/set", topic_udgang_set(B, 2, 2).c_str());
  TEST_ASSERT_EQUAL_STRING("ihc/ihc-bridge/output/+/+/set", topic_udgang_set_alle(B).c_str());
  TEST_ASSERT_EQUAL_STRING("ihc/ihc-bridge/status", topic_status(B).c_str());
  TEST_ASSERT_EQUAL_STRING("ihc/ihc-bridge/input/3/status", topic_indgangsmodul_status(B, 3).c_str());
}

void test_parse_set_topic() {
  uint32_t m = 0, k = 0;
  TEST_ASSERT_TRUE(parse_udgang_set(B, "ihc/ihc-bridge/output/2/2/set", m, k));
  TEST_ASSERT_EQUAL_UINT32(2, m);
  TEST_ASSERT_EQUAL_UINT32(2, k);
  TEST_ASSERT_TRUE(parse_udgang_set(B, "ihc/ihc-bridge/output/16/8/set", m, k));
  TEST_ASSERT_EQUAL_UINT32(16, m);
  TEST_ASSERT_EQUAL_UINT32(8, k);

  TEST_ASSERT_FALSE(parse_udgang_set(B, "ihc/ihc-bridge/output/2/2/state", m, k));
  TEST_ASSERT_FALSE(parse_udgang_set(B, "ihc/ihc-bridge/output/2/set", m, k));
  TEST_ASSERT_FALSE(parse_udgang_set(B, "ihc/ihc-bridge/output/0/1/set", m, k));
  TEST_ASSERT_FALSE(parse_udgang_set(B, "ihc/ihc-bridge/output/02/1/set", m, k));
  TEST_ASSERT_FALSE(parse_udgang_set(B, "ihc/ihc-bridge/output/a/1/set", m, k));
  TEST_ASSERT_FALSE(parse_udgang_set(B, "ihc/ihc-bridge/output/1/1/set/x", m, k));
  TEST_ASSERT_FALSE(parse_udgang_set(B, "andet/output/1/1/set", m, k));
  TEST_ASSERT_FALSE(parse_udgang_set(B, "ihc/ihc-bridge2/output/1/1/set", m, k));
}

void test_parse_kommando() {
  TEST_ASSERT_EQUAL(Kommando::kTaend, parse_kommando("ON", "ON", "OFF"));
  TEST_ASSERT_EQUAL(Kommando::kTaend, parse_kommando("on", "ON", "OFF"));
  TEST_ASSERT_EQUAL(Kommando::kTaend, parse_kommando(" 1 ", "ON", "OFF"));
  TEST_ASSERT_EQUAL(Kommando::kTaend, parse_kommando("true", "ON", "OFF"));
  TEST_ASSERT_EQUAL(Kommando::kSluk, parse_kommando("OFF", "ON", "OFF"));
  TEST_ASSERT_EQUAL(Kommando::kSluk, parse_kommando("0", "ON", "OFF"));
  TEST_ASSERT_EQUAL(Kommando::kSkift, parse_kommando("TOGGLE", "ON", "OFF"));
  TEST_ASSERT_EQUAL(Kommando::kUgyldig, parse_kommando("", "ON", "OFF"));
  TEST_ASSERT_EQUAL(Kommando::kUgyldig, parse_kommando("måske", "ON", "OFF"));
  // Egne payloads (f.eks. "tændt"/"slukket").
  TEST_ASSERT_EQUAL(Kommando::kTaend, parse_kommando("tændt", "tændt", "slukket"));
  TEST_ASSERT_EQUAL(Kommando::kSluk, parse_kommando("slukket", "tændt", "slukket"));
}

void test_basis_topic_validering() {
  TEST_ASSERT_TRUE(basis_topic_gyldig("ihc/ihc-bridge"));
  TEST_ASSERT_TRUE(basis_topic_gyldig("hus/ihc"));
  TEST_ASSERT_TRUE(basis_topic_gyldig("ihc"));
  TEST_ASSERT_FALSE(basis_topic_gyldig(""));
  TEST_ASSERT_FALSE(basis_topic_gyldig("/ihc"));
  TEST_ASSERT_FALSE(basis_topic_gyldig("ihc/"));
  TEST_ASSERT_FALSE(basis_topic_gyldig("ihc//bridge"));
  TEST_ASSERT_FALSE(basis_topic_gyldig("ihc/+"));
  TEST_ASSERT_FALSE(basis_topic_gyldig("ihc/#"));
  TEST_ASSERT_FALSE(basis_topic_gyldig("ihc bridge"));
}

void test_json_escape() {
  TEST_ASSERT_EQUAL_STRING("Køkken \\\"loft\\\"", json_escape("Køkken \"loft\"").c_str());
  TEST_ASSERT_EQUAL_STRING("a\\\\b", json_escape("a\\b").c_str());
  TEST_ASSERT_EQUAL_STRING("x\\u000ay", json_escape("x\ny").c_str());
}

void test_discovery_udgang() {
  const Enhed e{"ihcbridge_a1b2c3", "IHC Bridge", "2.0.0", "http://192.168.1.50"};
  TEST_ASSERT_EQUAL_STRING("homeassistant/switch/ihcbridge_a1b2c3/udgang_2_2/config",
                           discovery_topic_udgang("homeassistant", e, 2, 2).c_str());
  const std::string j = discovery_udgang(B, e, 2, 2, "Køkken loft", "ON", "OFF");
  TEST_ASSERT_EQUAL_STRING(
      "{\"name\":\"Køkken loft\",\"uniq_id\":\"ihcbridge_a1b2c3_udgang_2_2\","
      "\"obj_id\":\"ihcbridge_a1b2c3_udgang_2_2\",\"stat_t\":\"ihc/ihc-bridge/output/2/2/state\","
      "\"cmd_t\":\"ihc/ihc-bridge/output/2/2/set\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\","
      "\"avty_t\":\"ihc/ihc-bridge/status\",\"dev\":{\"ids\":[\"ihcbridge_a1b2c3\"],\"name\":\"IHC Bridge\","
      "\"mf\":\"IHC Bridge\",\"mdl\":\"ESP32-S3 IHC-controller\",\"sw\":\"2.0.0\",\"cu\":\"http://192.168.1.50\"}}",
      j.c_str());
}

void test_discovery_indgang() {
  const Enhed e{"ihcbridge_a1b2c3", "IHC Bridge", "2.0.0", ""};
  TEST_ASSERT_EQUAL_STRING("homeassistant/binary_sensor/ihcbridge_a1b2c3/indgang_3_15/config",
                           discovery_topic_indgang("homeassistant", e, 3, 15).c_str());
  const std::string j = discovery_indgang(B, e, 3, 15, "Indgang 3.15", "ON", "OFF");
  TEST_ASSERT_EQUAL_STRING(
      "{\"name\":\"Indgang 3.15\",\"uniq_id\":\"ihcbridge_a1b2c3_indgang_3_15\","
      "\"obj_id\":\"ihcbridge_a1b2c3_indgang_3_15\",\"stat_t\":\"ihc/ihc-bridge/input/3/15/state\","
      "\"pl_on\":\"ON\",\"pl_off\":\"OFF\",\"avty\":[{\"t\":\"ihc/ihc-bridge/status\"},"
      "{\"t\":\"ihc/ihc-bridge/input/3/status\"}],\"avty_mode\":\"all\",\"dev\":{\"ids\":[\"ihcbridge_a1b2c3\"],"
      "\"name\":\"IHC Bridge\",\"mf\":\"IHC Bridge\",\"mdl\":\"ESP32-S3 IHC-controller\",\"sw\":\"2.0.0\"}}",
      j.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_topics_som_i_eksemplet);
  RUN_TEST(test_parse_set_topic);
  RUN_TEST(test_parse_kommando);
  RUN_TEST(test_basis_topic_validering);
  RUN_TEST(test_json_escape);
  RUN_TEST(test_discovery_udgang);
  RUN_TEST(test_discovery_indgang);
  return UNITY_END();
}
