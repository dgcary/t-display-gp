#include <unity.h>

#include <string>

#include "BambuConfig.h"

void setUp() {}
void tearDown() {}

namespace {
BambuConfig enabledConfig() {
  BambuConfig cfg;
  cfg.enabled = true;
  cfg.region = BambuRegion::CHINA;
  cfg.accessToken = "token-value";
  cfg.cloudUserId = "u_123456";
  cfg.printerCount = 1;
  cfg.printers[0].serial = "01P00A123456789";
  cfg.printers[0].name = "Office P1S";
  cfg.activePrinterIndex = 0;
  return cfg;
}
}  // namespace

void test_disabled_default_is_valid() {
  BambuConfig cfg;
  TEST_ASSERT_TRUE(validateBambuConfig(cfg).ok());
  TEST_ASSERT_FALSE(cfg.enabled);
  TEST_ASSERT_EQUAL(BambuRegion::US_EU, cfg.region);
  TEST_ASSERT_EQUAL_UINT32(0, cfg.printerCount);
}

void test_enabled_requires_token_user_id_and_printer() {
  BambuConfig cfg;
  cfg.enabled = true;
  cfg.region = BambuRegion::CHINA;
  TEST_ASSERT_FALSE(validateBambuConfig(cfg).ok());

  cfg.accessToken = "token";
  TEST_ASSERT_FALSE(validateBambuConfig(cfg).ok());

  cfg.cloudUserId = "u_123";
  TEST_ASSERT_FALSE(validateBambuConfig(cfg).ok());

  cfg.printerCount = 1;
  cfg.printers[0].serial = "01P00A123456789";
  TEST_ASSERT_TRUE(validateBambuConfig(cfg).ok());
}

void test_active_printer_helper_returns_selected_slot() {
  BambuConfig cfg = enabledConfig();
  cfg.printerCount = 2;
  cfg.printers[1].serial = "03900A987654321";
  cfg.printers[1].name = "A1 mini";
  cfg.activePrinterIndex = 1;

  const BambuPrinterConfig* active = activeBambuPrinter(cfg);
  TEST_ASSERT_NOT_NULL(active);
  TEST_ASSERT_EQUAL_STRING("03900A987654321", active->serial.c_str());
  TEST_ASSERT_EQUAL_STRING("A1 mini", active->name.c_str());
}

void test_codec_round_trip_preserves_two_printers_and_active_selection() {
  BambuConfig cfg = enabledConfig();
  cfg.printerCount = 2;
  cfg.printers[1].serial = "03900A987654321";
  cfg.printers[1].name = "A1 mini";
  cfg.activePrinterIndex = 1;

  std::string encoded;
  TEST_ASSERT_TRUE(BambuConfigCodec::encode(cfg, encoded));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, encoded.find("\"schema\":2"));
  TEST_ASSERT_EQUAL(std::string::npos, encoded.find("password"));
  TEST_ASSERT_EQUAL(std::string::npos, encoded.find("email"));

  BambuConfig decoded;
  TEST_ASSERT_TRUE(BambuConfigCodec::decode(encoded, decoded));
  TEST_ASSERT_EQUAL(cfg.enabled, decoded.enabled);
  TEST_ASSERT_EQUAL(cfg.region, decoded.region);
  TEST_ASSERT_EQUAL_STRING(cfg.accessToken.c_str(), decoded.accessToken.c_str());
  TEST_ASSERT_EQUAL_STRING(cfg.cloudUserId.c_str(), decoded.cloudUserId.c_str());
  TEST_ASSERT_EQUAL_UINT32(2, decoded.printerCount);
  TEST_ASSERT_EQUAL_UINT32(1, decoded.activePrinterIndex);
  TEST_ASSERT_EQUAL_STRING("01P00A123456789", decoded.printers[0].serial.c_str());
  TEST_ASSERT_EQUAL_STRING("Office P1S", decoded.printers[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("03900A987654321", decoded.printers[1].serial.c_str());
  TEST_ASSERT_EQUAL_STRING("A1 mini", decoded.printers[1].name.c_str());
}

void test_schema_v1_migrates_token_user_id_and_single_printer() {
  const std::string legacy =
      R"json({"schema":1,"enabled":true,"region":"china","email":"18900000000","password":"old-password","access_token":"token-value","cloud_user_id":"u_123456","printer_serial":"01P00A123456789","printer_name":"Office P1S"})json";

  BambuConfig decoded;
  TEST_ASSERT_TRUE(BambuConfigCodec::decode(legacy, decoded));
  TEST_ASSERT_TRUE(decoded.enabled);
  TEST_ASSERT_EQUAL(BambuRegion::CHINA, decoded.region);
  TEST_ASSERT_EQUAL_STRING("token-value", decoded.accessToken.c_str());
  TEST_ASSERT_EQUAL_STRING("u_123456", decoded.cloudUserId.c_str());
  TEST_ASSERT_EQUAL_UINT32(1, decoded.printerCount);
  TEST_ASSERT_EQUAL_UINT32(0, decoded.activePrinterIndex);
  TEST_ASSERT_EQUAL_STRING("01P00A123456789", decoded.printers[0].serial.c_str());
  TEST_ASSERT_EQUAL_STRING("Office P1S", decoded.printers[0].name.c_str());
}

void test_duplicate_printer_serial_is_rejected() {
  BambuConfig cfg = enabledConfig();
  cfg.printerCount = 2;
  cfg.printers[1].serial = cfg.printers[0].serial;
  TEST_ASSERT_FALSE(validateBambuConfig(cfg).ok());
}

void test_unsafe_printer_serial_is_rejected() {
  BambuConfig cfg = enabledConfig();
  cfg.printers[0].serial = "01P bad/serial";
  TEST_ASSERT_FALSE(validateBambuConfig(cfg).ok());
}

void test_active_printer_index_must_be_in_range() {
  BambuConfig cfg = enabledConfig();
  cfg.activePrinterIndex = 1;
  TEST_ASSERT_FALSE(validateBambuConfig(cfg).ok());
}

void test_broker_mapping() {
  TEST_ASSERT_EQUAL_STRING("us.mqtt.bambulab.com", bambuBrokerForRegion(BambuRegion::US_EU));
  TEST_ASSERT_EQUAL_STRING("cn.mqtt.bambulab.com", bambuBrokerForRegion(BambuRegion::CHINA));
}

void test_decode_malformed_json_fails_closed_without_mutating_output() {
  BambuConfig out = enabledConfig();
  const std::string keep = out.accessToken;
  TEST_ASSERT_FALSE(BambuConfigCodec::decode("{broken", out));
  TEST_ASSERT_EQUAL_STRING(keep.c_str(), out.accessToken.c_str());
}

void test_length_limits_are_enforced() {
  BambuConfig cfg = enabledConfig();
  cfg.accessToken.assign(BambuConfigLimits::ACCESS_TOKEN + 1, 't');
  TEST_ASSERT_FALSE(validateBambuConfig(cfg).ok());

  cfg = enabledConfig();
  cfg.cloudUserId.assign(BambuConfigLimits::CLOUD_USER_ID + 1, 'u');
  TEST_ASSERT_FALSE(validateBambuConfig(cfg).ok());

  cfg = enabledConfig();
  cfg.printers[0].serial.assign(BambuConfigLimits::PRINTER_SERIAL + 1, 's');
  TEST_ASSERT_FALSE(validateBambuConfig(cfg).ok());

  cfg = enabledConfig();
  cfg.printerCount = BambuConfigLimits::PRINTER_COUNT + 1;
  TEST_ASSERT_FALSE(validateBambuConfig(cfg).ok());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_disabled_default_is_valid);
  RUN_TEST(test_enabled_requires_token_user_id_and_printer);
  RUN_TEST(test_active_printer_helper_returns_selected_slot);
  RUN_TEST(test_codec_round_trip_preserves_two_printers_and_active_selection);
  RUN_TEST(test_schema_v1_migrates_token_user_id_and_single_printer);
  RUN_TEST(test_duplicate_printer_serial_is_rejected);
  RUN_TEST(test_unsafe_printer_serial_is_rejected);
  RUN_TEST(test_active_printer_index_must_be_in_range);
  RUN_TEST(test_broker_mapping);
  RUN_TEST(test_decode_malformed_json_fails_closed_without_mutating_output);
  RUN_TEST(test_length_limits_are_enforced);
  return UNITY_END();
}
