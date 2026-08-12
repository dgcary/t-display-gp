#include <unity.h>

#include "MarketClock.h"

void setUp() {}
void tearDown() {}

void test_session_boundaries() {
  MarketClock clock;
  TEST_ASSERT_EQUAL(MarketStatus::PRE_OPEN,
                    clock.status({2026, 8, 11, 9, 20, 0, 2}, true));
  TEST_ASSERT_EQUAL(MarketStatus::TRADING_AM,
                    clock.status({2026, 8, 11, 9, 30, 0, 2}, true));
  TEST_ASSERT_EQUAL(MarketStatus::TRADING_AM,
                    clock.status({2026, 8, 11, 11, 30, 0, 2}, true));
  TEST_ASSERT_EQUAL(MarketStatus::LUNCH_BREAK,
                    clock.status({2026, 8, 11, 12, 0, 0, 2}, true));
  TEST_ASSERT_EQUAL(MarketStatus::TRADING_PM,
                    clock.status({2026, 8, 11, 13, 0, 0, 2}, true));
  TEST_ASSERT_EQUAL(MarketStatus::TRADING_PM,
                    clock.status({2026, 8, 11, 15, 0, 0, 2}, true));
  TEST_ASSERT_EQUAL(MarketStatus::CLOSED,
                    clock.status({2026, 8, 11, 15, 5, 0, 2}, true));
}

void test_non_trading_day_detection() {
  MarketClock clock;
  TEST_ASSERT_EQUAL(MarketStatus::NON_TRADING_DAY,
                    clock.status({2026, 8, 16, 10, 0, 0, 0}, false));
  TEST_ASSERT_EQUAL(MarketStatus::NON_TRADING_DAY,
                    clock.status({2026, 8, 17, 10, 0, 0, 1}, false));
}

void test_preopen_does_not_require_current_day_quote() {
  MarketClock clock;
  TEST_ASSERT_EQUAL(MarketStatus::PRE_OPEN,
                    clock.status({2026, 8, 17, 9, 20, 0, 1}, false));
}

void test_recommended_intervals() {
  MarketClock clock(4000);
  TEST_ASSERT_EQUAL_UINT32(4000, clock.recommendedQuoteIntervalMs(MarketStatus::TRADING_AM));
  TEST_ASSERT_EQUAL_UINT32(4000, clock.recommendedQuoteIntervalMs(MarketStatus::TRADING_PM));
  TEST_ASSERT_EQUAL_UINT32(60000, clock.recommendedQuoteIntervalMs(MarketStatus::PRE_OPEN));
  TEST_ASSERT_EQUAL_UINT32(60000, clock.recommendedQuoteIntervalMs(MarketStatus::LUNCH_BREAK));
  TEST_ASSERT_EQUAL_UINT32(300000, clock.recommendedQuoteIntervalMs(MarketStatus::CLOSED));
  TEST_ASSERT_EQUAL_UINT32(300000, clock.recommendedQuoteIntervalMs(MarketStatus::NON_TRADING_DAY));
  TEST_ASSERT_EQUAL_UINT32(15000, clock.recommendedQuoteIntervalMs(MarketStatus::UNKNOWN));
}

void test_configured_interval_is_clamped() {
  MarketClock tooFast(1000);
  MarketClock tooSlow(10000);
  TEST_ASSERT_EQUAL_UINT32(3000, tooFast.recommendedQuoteIntervalMs(MarketStatus::TRADING_AM));
  TEST_ASSERT_EQUAL_UINT32(5000, tooSlow.recommendedQuoteIntervalMs(MarketStatus::TRADING_PM));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_session_boundaries);
  RUN_TEST(test_non_trading_day_detection);
  RUN_TEST(test_preopen_does_not_require_current_day_quote);
  RUN_TEST(test_recommended_intervals);
  RUN_TEST(test_configured_interval_is_clamped);
  return UNITY_END();
}
