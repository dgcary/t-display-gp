#include <unity.h>

#include "ProviderFailover.h"

void setUp() {}
void tearDown() {}

void test_three_tencent_primary_failures_within_window_switch_to_eastmoney() {
  ProviderFailover failover;
  TEST_ASSERT_EQUAL(ProviderId::TENCENT, failover.activeProvider(0));
  failover.recordFailure(ProviderId::TENCENT, 1000);
  TEST_ASSERT_EQUAL(ProviderId::TENCENT, failover.activeProvider(1000));
  failover.recordFailure(ProviderId::TENCENT, 20000);
  TEST_ASSERT_EQUAL(ProviderId::TENCENT, failover.activeProvider(20000));
  failover.recordFailure(ProviderId::TENCENT, 59000);
  TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, failover.activeProvider(59000));
}

void test_tencent_failure_window_and_success_reset_consecutive_count() {
  ProviderFailover failover;
  failover.recordFailure(ProviderId::TENCENT, 1000);
  failover.recordFailure(ProviderId::TENCENT, 70000);
  failover.recordFailure(ProviderId::TENCENT, 80000);
  TEST_ASSERT_EQUAL(ProviderId::TENCENT, failover.activeProvider(80000));
  failover.recordSuccess(ProviderId::TENCENT, 81000);
  failover.recordFailure(ProviderId::TENCENT, 82000);
  failover.recordFailure(ProviderId::TENCENT, 83000);
  TEST_ASSERT_EQUAL(ProviderId::TENCENT, failover.activeProvider(83000));
}

void test_tencent_primary_probe_interval_and_two_success_recovery() {
  ProviderFailover failover;
  failover.recordFailure(ProviderId::TENCENT, 1000);
  failover.recordFailure(ProviderId::TENCENT, 2000);
  failover.recordFailure(ProviderId::TENCENT, 3000);
  TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, failover.activeProvider(3000));
  TEST_ASSERT_FALSE(failover.shouldProbePrimary(122999));
  TEST_ASSERT_TRUE(failover.shouldProbePrimary(123000));
  failover.recordPrimaryProbeAttempt(123000);
  TEST_ASSERT_FALSE(failover.shouldProbePrimary(242999));
  failover.recordSuccess(ProviderId::TENCENT, 123500);
  TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, failover.activeProvider(123500));
  TEST_ASSERT_TRUE(failover.shouldProbePrimary(243000));
  failover.recordPrimaryProbeAttempt(243000);
  failover.recordSuccess(ProviderId::TENCENT, 243500);
  TEST_ASSERT_EQUAL(ProviderId::TENCENT, failover.activeProvider(243500));
}

void test_failed_tencent_probe_resets_recovery_successes() {
  ProviderFailover failover;
  failover.recordFailure(ProviderId::TENCENT, 1);
  failover.recordFailure(ProviderId::TENCENT, 2);
  failover.recordFailure(ProviderId::TENCENT, 3);
  failover.recordPrimaryProbeAttempt(120003);
  failover.recordSuccess(ProviderId::TENCENT, 120100);
  failover.recordFailure(ProviderId::TENCENT, 120200);
  failover.recordPrimaryProbeAttempt(240003);
  failover.recordSuccess(ProviderId::TENCENT, 240100);
  TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, failover.activeProvider(240100));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_three_tencent_primary_failures_within_window_switch_to_eastmoney);
  RUN_TEST(test_tencent_failure_window_and_success_reset_consecutive_count);
  RUN_TEST(test_tencent_primary_probe_interval_and_two_success_recovery);
  RUN_TEST(test_failed_tencent_probe_resets_recovery_successes);
  return UNITY_END();
}
