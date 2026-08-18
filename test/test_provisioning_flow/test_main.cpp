#include <unity.h>

#include "ProvisioningFlow.h"

void setUp() {}
void tearDown() {}

void test_saved_credentials_accept_actual_connected_station() {
  TEST_ASSERT_EQUAL(ProvisioningNextAction::ACCEPT,
                    decideProvisioningNextAction({false, true, false, true, false}));
}

void test_forced_portal_requires_save_or_successful_portal_result() {
  TEST_ASSERT_EQUAL(ProvisioningNextAction::RETRY_PORTAL,
                    decideProvisioningNextAction({false, true, false, true, true}));
  TEST_ASSERT_EQUAL(ProvisioningNextAction::ACCEPT,
                    decideProvisioningNextAction({false, true, true, true, true}));
}

void test_failed_wifi_save_reopens_portal() {
  TEST_ASSERT_EQUAL(ProvisioningNextAction::RETRY_PORTAL,
                    decideProvisioningNextAction({false, false, true, true, true}));
}

void test_portal_result_mismatch_retries_instead_of_accepting() {
  TEST_ASSERT_EQUAL(ProvisioningNextAction::RETRY_PORTAL,
                    decideProvisioningNextAction({true, false, true, true, false}));
}

void test_invalid_config_always_reopens_portal() {
  TEST_ASSERT_EQUAL(ProvisioningNextAction::RETRY_PORTAL,
                    decideProvisioningNextAction({true, true, true, false, true}));
}

void test_clean_abort_without_connection_fails() {
  TEST_ASSERT_EQUAL(ProvisioningNextAction::FAIL,
                    decideProvisioningNextAction({false, false, false, true, false}));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_saved_credentials_accept_actual_connected_station);
  RUN_TEST(test_forced_portal_requires_save_or_successful_portal_result);
  RUN_TEST(test_failed_wifi_save_reopens_portal);
  RUN_TEST(test_portal_result_mismatch_retries_instead_of_accepting);
  RUN_TEST(test_invalid_config_always_reopens_portal);
  RUN_TEST(test_clean_abort_without_connection_fails);
  return UNITY_END();
}
