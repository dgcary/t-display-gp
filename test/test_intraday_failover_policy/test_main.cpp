#include <unity.h>

#include "MarketDataWorker.h"

void setUp() {}
void tearDown() {}

MarketRequest intraday(ProviderId provider, uint8_t attempt) {
  MarketRequest request;
  request.requestId = 42;
  request.type = MarketRequestType::INTRADAY;
  request.symbol = StockSymbol::parse("600519");
  request.provider = provider;
  request.attempt = attempt;
  request.priority = attempt > 1 ? MarketRequestPriority::INTRADAY_RETRY
                                 : MarketRequestPriority::INTRADAY;
  return request;
}

void test_eastmoney_transient_failure_retries_before_fallback() {
  ProviderDiagnostics diagnostics;
  const MarketRequest request = intraday(ProviderId::EAST_MONEY, 1);
  TEST_ASSERT_TRUE(MarketRequestPolicy::shouldRetryIntraday(request, ProviderError::NETWORK, diagnostics));
  TEST_ASSERT_FALSE(MarketRequestPolicy::shouldFallbackIntraday(request, ProviderError::NETWORK, diagnostics));
}

void test_eastmoney_final_transient_failure_falls_back_to_tencent() {
  ProviderDiagnostics diagnostics;
  const MarketRequest request = intraday(ProviderId::EAST_MONEY, BuildConfig::INTRADAY_MAX_ATTEMPTS);
  TEST_ASSERT_FALSE(MarketRequestPolicy::shouldRetryIntraday(request, ProviderError::NETWORK, diagnostics));
  TEST_ASSERT_TRUE(MarketRequestPolicy::shouldFallbackIntraday(request, ProviderError::NETWORK, diagnostics));
}

void test_eastmoney_nonretryable_failure_falls_back_immediately() {
  ProviderDiagnostics diagnostics;
  const MarketRequest request = intraday(ProviderId::EAST_MONEY, 1);
  TEST_ASSERT_FALSE(MarketRequestPolicy::shouldRetryIntraday(request, ProviderError::PARSE, diagnostics));
  TEST_ASSERT_TRUE(MarketRequestPolicy::shouldFallbackIntraday(request, ProviderError::PARSE, diagnostics));
}

void test_tencent_failure_never_recurses_or_falls_back_again() {
  ProviderDiagnostics diagnostics;
  const MarketRequest request = intraday(ProviderId::TENCENT, 1);
  TEST_ASSERT_FALSE(MarketRequestPolicy::shouldRetryIntraday(request, ProviderError::NETWORK, diagnostics));
  TEST_ASSERT_FALSE(MarketRequestPolicy::shouldFallbackIntraday(request, ProviderError::NETWORK, diagnostics));
}

void test_success_and_synthetic_results_never_fallback() {
  ProviderDiagnostics diagnostics;
  const MarketRequest request = intraday(ProviderId::EAST_MONEY, 3);
  TEST_ASSERT_FALSE(MarketRequestPolicy::shouldFallbackIntraday(request, ProviderError::NONE, diagnostics));
  TEST_ASSERT_FALSE(MarketRequestPolicy::shouldFallbackIntraday(request, ProviderError::CANCELLED, diagnostics));
  TEST_ASSERT_FALSE(MarketRequestPolicy::shouldFallbackIntraday(request, ProviderError::EXPIRED, diagnostics));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_eastmoney_transient_failure_retries_before_fallback);
  RUN_TEST(test_eastmoney_final_transient_failure_falls_back_to_tencent);
  RUN_TEST(test_eastmoney_nonretryable_failure_falls_back_immediately);
  RUN_TEST(test_tencent_failure_never_recurses_or_falls_back_again);
  RUN_TEST(test_success_and_synthetic_results_never_fallback);
  return UNITY_END();
}
