#include <unity.h>

#include <cstdint>

#include "BambuSessionModel.h"

void setUp() {}
void tearDown() {}

void test_mqtt_auth_failure_relogs_immediately_when_password_is_available() {
  BambuSessionModel model;
  model.setAutomaticReloginAvailable(true);

  model.onMqttAuthFailure(1000U);
  TEST_ASSERT_EQUAL(BambuSessionState::RELOGIN_PENDING, model.state());
  TEST_ASSERT_TRUE(model.shouldRelogin(1000U));

  model.onReloginStarted();
  TEST_ASSERT_EQUAL(BambuSessionState::RELOGIN_IN_PROGRESS, model.state());
}

void test_mqtt_auth_failure_stays_token_invalid_without_saved_password() {
  BambuSessionModel model;
  model.setAutomaticReloginAvailable(false);

  model.onMqttAuthFailure(1000U);
  TEST_ASSERT_EQUAL(BambuSessionState::TOKEN_INVALID, model.state());
  TEST_ASSERT_FALSE(model.shouldRelogin(1000U));
  TEST_ASSERT_FALSE(model.shouldRelogin(0xFFFFFFFFU));
}

void test_failed_automatic_relogin_uses_capped_backoff_sequence() {
  BambuSessionModel model;
  model.setAutomaticReloginAvailable(true);
  model.onMqttAuthFailure(1000U);
  model.onReloginStarted();

  model.onReloginFailure(2000U, BambuReloginFailure::NETWORK);
  TEST_ASSERT_EQUAL(BambuSessionState::NETWORK_ERROR, model.state());
  TEST_ASSERT_EQUAL_UINT32(60000U, model.reloginDelayMs());
  TEST_ASSERT_FALSE(model.shouldRelogin(61999U));
  TEST_ASSERT_TRUE(model.shouldRelogin(62000U));

  model.onReloginStarted();
  model.onReloginFailure(62000U, BambuReloginFailure::INVALID_CREDENTIALS);
  TEST_ASSERT_EQUAL(BambuSessionState::LOGIN_FAILED, model.state());
  TEST_ASSERT_EQUAL_UINT32(300000U, model.reloginDelayMs());
  TEST_ASSERT_FALSE(model.shouldRelogin(361999U));
  TEST_ASSERT_TRUE(model.shouldRelogin(362000U));

  model.onReloginStarted();
  model.onReloginFailure(362000U, BambuReloginFailure::SERVICE_ERROR);
  TEST_ASSERT_EQUAL_UINT32(900000U, model.reloginDelayMs());
  TEST_ASSERT_TRUE(model.shouldRelogin(1262000U));

  model.onReloginStarted();
  model.onReloginFailure(1262000U, BambuReloginFailure::NETWORK);
  TEST_ASSERT_EQUAL_UINT32(1800000U, model.reloginDelayMs());
  TEST_ASSERT_TRUE(model.shouldRelogin(3062000U));

  model.onReloginStarted();
  model.onReloginFailure(3062000U, BambuReloginFailure::NETWORK);
  TEST_ASSERT_EQUAL_UINT32(1800000U, model.reloginDelayMs());
  TEST_ASSERT_EQUAL_UINT8(5U, model.reloginFailureCount());
}

void test_success_resets_backoff_before_mqtt_reconnect() {
  BambuSessionModel model;
  model.setAutomaticReloginAvailable(true);
  model.onMqttAuthFailure(100U);
  model.onReloginStarted();
  model.onReloginFailure(200U, BambuReloginFailure::NETWORK);
  model.onReloginStarted();
  model.onReloginFailure(60200U, BambuReloginFailure::NETWORK);

  TEST_ASSERT_EQUAL_UINT8(2U, model.reloginFailureCount());
  TEST_ASSERT_EQUAL_UINT32(300000U, model.reloginDelayMs());

  model.onReloginSuccess();
  TEST_ASSERT_EQUAL(BambuSessionState::MQTT_CONNECTING, model.state());
  TEST_ASSERT_EQUAL_UINT8(0U, model.reloginFailureCount());
  TEST_ASSERT_EQUAL_UINT32(0U, model.reloginDelayMs());
  TEST_ASSERT_FALSE(model.shouldRelogin(0xFFFFFFFFU));

  model.onMqttAuthFailure(900000U);
  TEST_ASSERT_TRUE(model.shouldRelogin(900000U));
  model.onReloginStarted();
  model.onReloginFailure(900001U, BambuReloginFailure::NETWORK);
  TEST_ASSERT_EQUAL_UINT32(60000U, model.reloginDelayMs());
}

void test_two_factor_required_is_terminal_for_automatic_renewal() {
  BambuSessionModel model;
  model.setAutomaticReloginAvailable(true);
  model.onMqttAuthFailure(500U);
  model.onReloginStarted();
  model.onReloginFailure(600U, BambuReloginFailure::TWO_FACTOR_REQUIRED);

  TEST_ASSERT_EQUAL(BambuSessionState::TWO_FACTOR_REQUIRED, model.state());
  TEST_ASSERT_FALSE(model.shouldRelogin(600U));
  TEST_ASSERT_FALSE(model.shouldRelogin(0xFFFFFFFFU));
}

void test_backoff_deadline_is_wrap_safe() {
  BambuSessionModel model;
  model.setAutomaticReloginAvailable(true);
  model.onMqttAuthFailure(0xFFFF0000U);
  model.onReloginStarted();
  model.onReloginFailure(0xFFFFF000U, BambuReloginFailure::NETWORK);

  TEST_ASSERT_EQUAL_UINT32(60000U, model.reloginDelayMs());
  TEST_ASSERT_FALSE(model.shouldRelogin(0x00005000U));
  TEST_ASSERT_TRUE(model.shouldRelogin(0x0000DA60U));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_mqtt_auth_failure_relogs_immediately_when_password_is_available);
  RUN_TEST(test_mqtt_auth_failure_stays_token_invalid_without_saved_password);
  RUN_TEST(test_failed_automatic_relogin_uses_capped_backoff_sequence);
  RUN_TEST(test_success_resets_backoff_before_mqtt_reconnect);
  RUN_TEST(test_two_factor_required_is_terminal_for_automatic_renewal);
  RUN_TEST(test_backoff_deadline_is_wrap_safe);
  return UNITY_END();
}
