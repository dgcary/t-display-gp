#include <unity.h>

#include <string>

#include "BambuConfig.h"

void setUp() {}
void tearDown() {}

void test_disabled_default_is_valid() {
  BambuConfig cfg;
  TEST_ASSERT_TRUE(validateBambuConfig(cfg).ok());
  TEST_ASSERT_FALSE(cfg.enabled);
  TEST_ASSERT_EQUAL(BambuRegion::US_EU, cfg.region);
}

void test_enabled_requires_email_and_credential() {
  BambuConfig cfg;
  cfg.enabled = true;
  cfg.email = "user@example.com";
  TEST_ASSERT_FALSE(validateBambuConfig(cfg).ok());
  cfg.password = "secret";
  TEST_ASSERT_TRUE(validateBambuConfig(cfg).ok());
}

void test_token_can_satisfy_enabled_credential_without_password() {
  BambuConfig cfg;
  cfg.enabled = true;
  cfg.email = "user@example.com";
  cfg.accessToken = "token";
  TEST_ASSERT_TRUE(validateBambuConfig(cfg).ok());
}

void test_broker_mapping() {
  TEST_ASSERT_EQUAL_STRING("us.mqtt.bambulab.com", bambuBrokerForRegion(BambuRegion::US_EU));
  TEST_ASSERT_EQUAL_STRING("cn.mqtt.bambulab.com", bambuBrokerForRegion(BambuRegion::CHINA));
}

void test_codec_round_trip_preserves_all_fields() {
  BambuConfig cfg;
  cfg.enabled = true;
  cfg.region = BambuRegion::CHINA;
  cfg.email = "cary@example.com";
  cfg.password = "password-value";
  cfg.accessToken = "token-value";
  cfg.cloudUserId = "u_123456";
  cfg.printerSerial = "01P00A123456789";
  cfg.printerName = "Office P1S";

  std::string encoded;
  TEST_ASSERT_TRUE(BambuConfigCodec::encode(cfg, encoded));
  BambuConfig decoded;
  TEST_ASSERT_TRUE(BambuConfigCodec::decode(encoded, decoded));
  TEST_ASSERT_EQUAL(cfg.enabled, decoded.enabled);
  TEST_ASSERT_EQUAL(cfg.region, decoded.region);
  TEST_ASSERT_EQUAL_STRING(cfg.email.c_str(), decoded.email.c_str());
  TEST_ASSERT_EQUAL_STRING(cfg.password.c_str(), decoded.password.c_str());
  TEST_ASSERT_EQUAL_STRING(cfg.accessToken.c_str(), decoded.accessToken.c_str());
  TEST_ASSERT_EQUAL_STRING(cfg.cloudUserId.c_str(), decoded.cloudUserId.c_str());
  TEST_ASSERT_EQUAL_STRING(cfg.printerSerial.c_str(), decoded.printerSerial.c_str());
  TEST_ASSERT_EQUAL_STRING(cfg.printerName.c_str(), decoded.printerName.c_str());
}

void test_decode_malformed_json_fails_closed_without_mutating_output() {
  BambuConfig out;
  out.email = "keep-me";
  TEST_ASSERT_FALSE(BambuConfigCodec::decode("{broken", out));
  TEST_ASSERT_EQUAL_STRING("keep-me", out.email.c_str());
}

void test_length_limits_are_enforced() {
  BambuConfig cfg;
  cfg.enabled = true;
  cfg.email.assign(161, 'a');
  cfg.password = "x";
  TEST_ASSERT_FALSE(validateBambuConfig(cfg).ok());

  cfg.email = "u@example.com";
  cfg.password.assign(257, 'p');
  TEST_ASSERT_FALSE(validateBambuConfig(cfg).ok());

  cfg.password = "ok";
  cfg.printerSerial.assign(33, 's');
  TEST_ASSERT_FALSE(validateBambuConfig(cfg).ok());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_disabled_default_is_valid);
  RUN_TEST(test_enabled_requires_email_and_credential);
  RUN_TEST(test_token_can_satisfy_enabled_credential_without_password);
  RUN_TEST(test_broker_mapping);
  RUN_TEST(test_codec_round_trip_preserves_all_fields);
  RUN_TEST(test_decode_malformed_json_fails_closed_without_mutating_output);
  RUN_TEST(test_length_limits_are_enforced);
  return UNITY_END();
}
