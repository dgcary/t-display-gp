#include <unity.h>
#include "MarketDataWorker.h"

void setUp() {}
void tearDown() {}

MarketRequest request(const char* symbol, MarketRequestType type, ProviderId provider=ProviderId::EAST_MONEY) {
  return {1, type, StockSymbol::parse(symbol), provider};
}

void test_pending_deduplicates_same_symbol_and_type() {
  PendingMarketRequests pending(8);
  const auto q = request("600519", MarketRequestType::QUOTE);
  TEST_ASSERT_TRUE(pending.add(q));
  TEST_ASSERT_FALSE(pending.add(q));
  TEST_ASSERT_TRUE(pending.add(request("600519", MarketRequestType::INTRADAY)));
  TEST_ASSERT_TRUE(pending.add(request("000001", MarketRequestType::QUOTE)));
}

void test_pending_remove_reopens_slot() {
  PendingMarketRequests pending(2);
  const auto a = request("600519", MarketRequestType::QUOTE);
  const auto b = request("000001", MarketRequestType::QUOTE);
  const auto c = request("300750", MarketRequestType::QUOTE);
  TEST_ASSERT_TRUE(pending.add(a));
  TEST_ASSERT_TRUE(pending.add(b));
  TEST_ASSERT_FALSE(pending.add(c));
  pending.remove(a);
  TEST_ASSERT_TRUE(pending.add(c));
}

void test_provider_does_not_change_dedupe_key() {
  PendingMarketRequests pending(8);
  TEST_ASSERT_TRUE(pending.add(request("600519", MarketRequestType::PRIMARY_PROBE, ProviderId::EAST_MONEY)));
  TEST_ASSERT_FALSE(pending.add(request("600519", MarketRequestType::PRIMARY_PROBE, ProviderId::TENCENT)));
}

void test_market_result_carries_request_diagnostics() {
  MarketResult result;
  result.provider = ProviderId::TENCENT;
  result.attempt = 2;
  result.queueWaitMs = 123;
  result.diagnostics.httpStatus = 200;
  result.diagnostics.nativeError = -11;
  result.diagnostics.receivedBytes = 8192;

  TEST_ASSERT_EQUAL(ProviderId::TENCENT, result.provider);
  TEST_ASSERT_EQUAL_UINT8(2, result.attempt);
  TEST_ASSERT_EQUAL_UINT32(123, result.queueWaitMs);
  TEST_ASSERT_EQUAL(-11, result.diagnostics.nativeError);
  TEST_ASSERT_EQUAL_UINT32(8192, result.diagnostics.receivedBytes);
}

int main(){
  UNITY_BEGIN();
  RUN_TEST(test_pending_deduplicates_same_symbol_and_type);
  RUN_TEST(test_pending_remove_reopens_slot);
  RUN_TEST(test_provider_does_not_change_dedupe_key);
  RUN_TEST(test_market_result_carries_request_diagnostics);
  return UNITY_END();
}
