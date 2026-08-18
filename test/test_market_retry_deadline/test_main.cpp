#include <unity.h>

#include <cstdint>

#include "MarketDataWorker.h"

void setUp() {}
void tearDown() {}

MarketRequest retryRequest(uint32_t cycleStartedMs, uint32_t createdMs) {
  MarketRequest request;
  request.requestId = 7;
  request.type = MarketRequestType::INTRADAY;
  request.symbol = StockSymbol::parse("600519");
  request.provider = ProviderId::EAST_MONEY;
  request.createdMs = createdMs;
  request.notBeforeMs = createdMs;
  request.cycleStartedMs = cycleStartedMs;
  request.attempt = 2;
  request.priority = MarketRequestPriority::INTRADAY_RETRY;
  return request;
}

void test_retry_cycle_start_zero_is_a_valid_millis_value() {
  const MarketRequest retry = retryRequest(0, 5000);
  TEST_ASSERT_FALSE(MarketRequestPolicy::expired(retry, 15000));
  TEST_ASSERT_TRUE(MarketRequestPolicy::expired(retry, 15001));
}

void test_retry_deadline_survives_millis_wraparound() {
  const uint32_t cycleStart = UINT32_MAX - 5000U;
  const MarketRequest retry = retryRequest(cycleStart, 1000);
  TEST_ASSERT_FALSE(MarketRequestPolicy::expired(retry, 9999));
  TEST_ASSERT_TRUE(MarketRequestPolicy::expired(retry, 10000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_retry_cycle_start_zero_is_a_valid_millis_value);
  RUN_TEST(test_retry_deadline_survives_millis_wraparound);
  return UNITY_END();
}
