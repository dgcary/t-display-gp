#include <unity.h>

#include <vector>

#include "MarketDataWorker.h"

void setUp() {}
void tearDown() {}

MarketRequest quote(uint32_t id, MarketRequestPriority priority, uint32_t createdMs) {
  MarketRequest request;
  request.requestId = id;
  request.type = MarketRequestType::QUOTE;
  request.symbol = StockSymbol::parse("000001");
  request.provider = ProviderId::EAST_MONEY;
  request.createdMs = createdMs;
  request.notBeforeMs = createdMs;
  request.cycleStartedMs = createdMs;
  request.priority = priority;
  return request;
}

void test_current_quote_replaces_pending_background_same_symbol() {
  PendingMarketWork pending(8);
  const MarketRequest background = quote(1, MarketRequestPriority::BACKGROUND_QUOTE, 1000);
  const MarketRequest current = quote(2, MarketRequestPriority::CURRENT_QUOTE, 1100);

  TEST_ASSERT_TRUE(pending.add(background).accepted);
  const PendingAddResult promoted = pending.add(current);
  TEST_ASSERT_TRUE(promoted.accepted);
  TEST_ASSERT_TRUE(promoted.replaced);
  TEST_ASSERT_EQUAL_UINT32(1, promoted.replacedRequest.requestId);

  MarketRequest next;
  std::vector<MarketRequest> expired;
  TEST_ASSERT_TRUE(pending.popNextReady(1100, next, expired));
  TEST_ASSERT_EQUAL_UINT32(2, next.requestId);
  TEST_ASSERT_EQUAL(MarketRequestPriority::CURRENT_QUOTE, next.priority);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_current_quote_replaces_pending_background_same_symbol);
  return UNITY_END();
}
