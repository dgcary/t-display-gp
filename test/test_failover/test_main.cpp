#include <unity.h>

#include "ProviderFailover.h"

void setUp() {}
void tearDown() {}

void test_three_primary_failures_within_window_switch_to_tencent() {
  ProviderFailover failover;
  TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, failover.activeProvider(0));
  failover.recordFailure(ProviderId::EAST_MONEY, 1000);
  TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, failover.activeProvider(1000));
  failover.recordFailure(ProviderId::EAST_MONEY, 20000);
  TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, failover.activeProvider(20000));
  failover.recordFailure(ProviderId::EAST_MONEY, 59000);
  TEST_ASSERT_EQUAL(ProviderId::TENCENT, failover.activeProvider(59000));
}

void test_failure_window_and_success_reset_consecutive_count() {
  ProviderFailover failover;
  failover.recordFailure(ProviderId::EAST_MONEY, 1000);
  failover.recordFailure(ProviderId::EAST_MONEY, 70000);
  failover.recordFailure(ProviderId::EAST_MONEY, 80000);
  TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, failover.activeProvider(80000));
  failover.recordSuccess(ProviderId::EAST_MONEY, 81000);
  failover.recordFailure(ProviderId::EAST_MONEY, 82000);
  failover.recordFailure(ProviderId::EAST_MONEY, 83000);
  TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, failover.activeProvider(83000));
}

void test_primary_probe_interval_and_two_success_recovery() {
  ProviderFailover failover;
  failover.recordFailure(ProviderId::EAST_MONEY, 1000);
  failover.recordFailure(ProviderId::EAST_MONEY, 2000);
  failover.recordFailure(ProviderId::EAST_MONEY, 3000);
  TEST_ASSERT_EQUAL(ProviderId::TENCENT, failover.activeProvider(3000));
  TEST_ASSERT_FALSE(failover.shouldProbePrimary(122999));
  TEST_ASSERT_TRUE(failover.shouldProbePrimary(123000));
  failover.recordPrimaryProbeAttempt(123000);
  TEST_ASSERT_FALSE(failover.shouldProbePrimary(242999));
  failover.recordSuccess(ProviderId::EAST_MONEY, 123500);
  TEST_ASSERT_EQUAL(ProviderId::TENCENT, failover.activeProvider(123500));
  TEST_ASSERT_TRUE(failover.shouldProbePrimary(243000));
  failover.recordPrimaryProbeAttempt(243000);
  failover.recordSuccess(ProviderId::EAST_MONEY, 243500);
  TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, failover.activeProvider(243500));
}

void test_failed_probe_resets_recovery_successes() {
  ProviderFailover failover;
  failover.recordFailure(ProviderId::EAST_MONEY, 1);
  failover.recordFailure(ProviderId::EAST_MONEY, 2);
  failover.recordFailure(ProviderId::EAST_MONEY, 3);
  failover.recordPrimaryProbeAttempt(120003);
  failover.recordSuccess(ProviderId::EAST_MONEY, 120100);
  failover.recordFailure(ProviderId::EAST_MONEY, 120200);
  failover.recordPrimaryProbeAttempt(240003);
  failover.recordSuccess(ProviderId::EAST_MONEY, 240100);
  TEST_ASSERT_EQUAL(ProviderId::TENCENT, failover.activeProvider(240100));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_three_primary_failures_within_window_switch_to_tencent);
  RUN_TEST(test_failure_window_and_success_reset_consecutive_count);
  RUN_TEST(test_primary_probe_interval_and_two_success_recovery);
  RUN_TEST(test_failed_probe_resets_recovery_successes);
  return UNITY_END();
}
