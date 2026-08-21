#include <unity.h>

#include <string>

#include "HomeAssistantConfig.h"

void setUp() {}
void tearDown() {}

HomeAssistantConfig validHttpsConfig() {
  HomeAssistantConfig config;
  config.enabled = true;
  config.baseUrl = "https://ha.example.test:8123";
  config.token = "long-lived-token";
  config.caCert = "-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----\n";
  config.refreshSeconds = 30;
  config.entityCount = 2;
  config.entities[0] = {"sensor.living_temperature", "客厅温度"};
  config.entities[1] = {"binary_sensor.front_door", "前门"};
  return config;
}

HomeAssistantConfig validHttpConfig() {
  HomeAssistantConfig config = validHttpsConfig();
  config.baseUrl = "http://homeassistant.local:8123";
  config.caCert.clear();
  return config;
}

void test_disabled_default_is_valid_and_has_no_required_secret() {
  HomeAssistantConfig config;
  TEST_ASSERT_TRUE(validateHomeAssistantConfig(config).ok());
}

void test_enabled_http_server_requires_token_and_entity_but_not_ca() {
  HomeAssistantConfig config = validHttpConfig();
  TEST_ASSERT_TRUE(validateHomeAssistantConfig(config).ok());
  config.token.clear();
  TEST_ASSERT_EQUAL(HomeAssistantConfigError::TOKEN, validateHomeAssistantConfig(config).error);
  config = validHttpConfig();
  config.entityCount = 0;
  TEST_ASSERT_EQUAL(HomeAssistantConfigError::ENTITY_COUNT, validateHomeAssistantConfig(config).error);
}

void test_enabled_https_server_requires_ca() {
  HomeAssistantConfig config = validHttpsConfig();
  TEST_ASSERT_TRUE(validateHomeAssistantConfig(config).ok());
  config.caCert.clear();
  TEST_ASSERT_EQUAL(HomeAssistantConfigError::CA_CERT, validateHomeAssistantConfig(config).error);
}

void test_enabled_rejects_unknown_scheme_noncanonical_or_bad_entity_id() {
  HomeAssistantConfig config = validHttpsConfig();
  config.baseUrl = "ftp://ha.example.test";
  TEST_ASSERT_EQUAL(HomeAssistantConfigError::BASE_URL, validateHomeAssistantConfig(config).error);
  config = validHttpConfig();
  config.baseUrl += "/";
  TEST_ASSERT_EQUAL(HomeAssistantConfigError::BASE_URL, validateHomeAssistantConfig(config).error);
  config = validHttpConfig();
  config.entities[0].entityId = "Sensor.Bad-ID";
  TEST_ASSERT_EQUAL(HomeAssistantConfigError::ENTITY_ID, validateHomeAssistantConfig(config).error);
}

void test_codec_round_trip_keeps_http_mode_without_ca() {
  const HomeAssistantConfig original = validHttpConfig();
  std::string encoded;
  TEST_ASSERT_TRUE(HomeAssistantConfigCodec::encode(original, encoded));
  HomeAssistantConfig decoded;
  TEST_ASSERT_TRUE(HomeAssistantConfigCodec::decode(encoded, decoded));
  TEST_ASSERT_TRUE(decoded.enabled);
  TEST_ASSERT_EQUAL_STRING(original.baseUrl.c_str(), decoded.baseUrl.c_str());
  TEST_ASSERT_EQUAL_STRING(original.token.c_str(), decoded.token.c_str());
  TEST_ASSERT_TRUE(decoded.caCert.empty());
  TEST_ASSERT_EQUAL_UINT32(2, decoded.entityCount);
  TEST_ASSERT_EQUAL_STRING("sensor.living_temperature", decoded.entities[0].entityId.c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_disabled_default_is_valid_and_has_no_required_secret);
  RUN_TEST(test_enabled_http_server_requires_token_and_entity_but_not_ca);
  RUN_TEST(test_enabled_https_server_requires_ca);
  RUN_TEST(test_enabled_rejects_unknown_scheme_noncanonical_or_bad_entity_id);
  RUN_TEST(test_codec_round_trip_keeps_http_mode_without_ca);
  return UNITY_END();
}
