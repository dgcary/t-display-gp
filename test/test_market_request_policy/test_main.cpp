#include <unity.h>

#include <vector>

#include "MarketDataWorker.h"
#include "build_config.h"

void setUp() {}
void tearDown() {}

MarketRequest makeRequest(uint32_t id, const char* code, MarketRequestType type,
                          MarketRequestPriority priority, uint32_t created=1000,
                          uint32_t notBefore=1000, uint8_t attempt=1) {
  MarketRequest request;
  request.requestId=id;
  request.type=type;
  request.symbol=StockSymbol::parse(code);
  request.provider=ProviderId::EAST_MONEY;
  request.createdMs=created;
  request.notBeforeMs=notBefore;
  request.attempt=attempt;
  request.priority=priority;
  return request;
}

void test_quote_priority_beats_waiting_intraday() {
  PendingMarketWork pending(8);
  auto trend=makeRequest(1,"600519",MarketRequestType::INTRADAY,MarketRequestPriority::INTRADAY);
  auto quote=makeRequest(2,"000001",MarketRequestType::QUOTE,MarketRequestPriority::CURRENT_QUOTE);
  TEST_ASSERT_TRUE(pending.add(trend).accepted);
  TEST_ASSERT_TRUE(pending.add(quote).accepted);
  MarketRequest next; std::vector<MarketRequest> expired;
  TEST_ASSERT_TRUE(pending.popNextReady(1000,next,expired));
  TEST_ASSERT_EQUAL_UINT32(2,next.requestId);
}

void test_latest_current_page_quote_wins_same_priority() {
  PendingMarketWork pending(8);
  auto oldPage=makeRequest(1,"600519",MarketRequestType::QUOTE,MarketRequestPriority::CURRENT_QUOTE,1000,1000);
  auto newPage=makeRequest(2,"000001",MarketRequestType::QUOTE,MarketRequestPriority::CURRENT_QUOTE,1100,1100);
  TEST_ASSERT_TRUE(pending.add(oldPage).accepted);
  TEST_ASSERT_TRUE(pending.add(newPage).accepted);
  MarketRequest next; std::vector<MarketRequest> expired;
  TEST_ASSERT_TRUE(pending.popNextReady(1100,next,expired));
  TEST_ASSERT_EQUAL_UINT32(2,next.requestId);
}

void test_intraday_pending_is_latest_wins() {
  PendingMarketWork pending(8);
  auto first=makeRequest(1,"600519",MarketRequestType::INTRADAY,MarketRequestPriority::INTRADAY);
  auto latest=makeRequest(2,"000001",MarketRequestType::INTRADAY,MarketRequestPriority::INTRADAY);
  TEST_ASSERT_TRUE(pending.add(first).accepted);
  const PendingAddResult replaced=pending.add(latest);
  TEST_ASSERT_TRUE(replaced.accepted);
  TEST_ASSERT_TRUE(replaced.replaced);
  TEST_ASSERT_EQUAL_UINT32(1,replaced.replacedRequest.requestId);
  TEST_ASSERT_EQUAL_UINT32(1,pending.intradayCount());
  MarketRequest next; std::vector<MarketRequest> expired;
  TEST_ASSERT_TRUE(pending.popNextReady(1000,next,expired));
  TEST_ASSERT_EQUAL_UINT32(2,next.requestId);
}

void test_expired_requests_are_returned_without_execution() {
  PendingMarketWork pending(8);
  auto quote=makeRequest(1,"600519",MarketRequestType::QUOTE,MarketRequestPriority::CURRENT_QUOTE,1000,1000);
  auto trend=makeRequest(2,"000001",MarketRequestType::INTRADAY,MarketRequestPriority::INTRADAY,1000,1000);
  TEST_ASSERT_TRUE(pending.add(quote).accepted);
  TEST_ASSERT_TRUE(pending.add(trend).accepted);
  MarketRequest next; std::vector<MarketRequest> expired;
  TEST_ASSERT_FALSE(pending.popNextReady(1000 + BuildConfig::QUOTE_REQUEST_TTL_MS + 1,next,expired));
  TEST_ASSERT_EQUAL_UINT32(2,expired.size());
}

void test_retry_classification_and_limits() {
  ProviderDiagnostics diagnostics;
  TEST_ASSERT_TRUE(MarketRequestPolicy::retryable(ProviderError::NETWORK,diagnostics));
  diagnostics.httpStatus=503;
  TEST_ASSERT_TRUE(MarketRequestPolicy::retryable(ProviderError::HTTP_STATUS,diagnostics));
  diagnostics.httpStatus=408;
  TEST_ASSERT_TRUE(MarketRequestPolicy::retryable(ProviderError::HTTP_STATUS,diagnostics));
  diagnostics.httpStatus=404;
  TEST_ASSERT_FALSE(MarketRequestPolicy::retryable(ProviderError::HTTP_STATUS,diagnostics));
  TEST_ASSERT_FALSE(MarketRequestPolicy::retryable(ProviderError::PARSE,diagnostics));
  TEST_ASSERT_FALSE(MarketRequestPolicy::retryable(ProviderError::BODY_TOO_LARGE,diagnostics));
}

void test_retry_delay_is_bounded_and_only_attempts_two_and_three_exist() {
  for(uint32_t id=1; id<50; ++id) {
    const uint32_t first=MarketRequestPolicy::retryDelayMs(2,id);
    TEST_ASSERT_TRUE(first >= 1200 && first <= 1800);
    const uint32_t second=MarketRequestPolicy::retryDelayMs(3,id);
    TEST_ASSERT_TRUE(second >= 3200 && second <= 4800);
  }
  TEST_ASSERT_EQUAL_UINT32(0,MarketRequestPolicy::retryDelayMs(1,1));
  TEST_ASSERT_EQUAL_UINT32(0,MarketRequestPolicy::retryDelayMs(4,1));
  TEST_ASSERT_EQUAL_UINT8(3,BuildConfig::INTRADAY_MAX_ATTEMPTS);
}

void test_not_before_defers_retry_but_allows_quote() {
  PendingMarketWork pending(8);
  auto retry=makeRequest(1,"600519",MarketRequestType::INTRADAY,MarketRequestPriority::INTRADAY_RETRY,2000,6000,2);
  auto quote=makeRequest(2,"600519",MarketRequestType::QUOTE,MarketRequestPriority::CURRENT_QUOTE,3000,3000);
  TEST_ASSERT_TRUE(pending.add(retry).accepted);
  TEST_ASSERT_TRUE(pending.add(quote).accepted);
  MarketRequest next; std::vector<MarketRequest> expired;
  TEST_ASSERT_TRUE(pending.popNextReady(3000,next,expired));
  TEST_ASSERT_EQUAL_UINT32(2,next.requestId);
  TEST_ASSERT_FALSE(pending.popNextReady(5000,next,expired));
  TEST_ASSERT_TRUE(pending.popNextReady(6000,next,expired));
  TEST_ASSERT_EQUAL_UINT32(1,next.requestId);
}

int main(){
  UNITY_BEGIN();
  RUN_TEST(test_quote_priority_beats_waiting_intraday);
  RUN_TEST(test_latest_current_page_quote_wins_same_priority);
  RUN_TEST(test_intraday_pending_is_latest_wins);
  RUN_TEST(test_expired_requests_are_returned_without_execution);
  RUN_TEST(test_retry_classification_and_limits);
  RUN_TEST(test_retry_delay_is_bounded_and_only_attempts_two_and_three_exist);
  RUN_TEST(test_not_before_defers_retry_but_allows_quote);
  return UNITY_END();
}
