#include <unity.h>

#include <deque>
#include <vector>

#include "StockController.h"

void setUp() {}
void tearDown() {}

class Queue final : public IMarketDataQueue {
 public:
  std::vector<MarketRequest> requests;
  std::deque<MarketResult> results;
  bool enqueue(const MarketRequest& request) override { requests.push_back(request); return true; }
  bool tryReceive(MarketResult& result) override {
    if (results.empty()) return false;
    result = std::move(results.front());
    results.pop_front();
    return true;
  }
};

AppConfig config() {
  AppConfig c; c.quoteRefreshSec=5;
  c.stocks={{StockSymbol::parse("600519"),"茅台"},
            {StockSymbol::parse("000001"),"平安"},
            {StockSymbol::parse("300750"),"宁德"}};
  return c;
}

LocalDateTime trading(){ return {2026,8,11,10,0,0,2}; }

const MarketRequest* find(const Queue& q, const char* code, MarketRequestType type) {
  for (auto it=q.requests.rbegin(); it!=q.requests.rend(); ++it) {
    if (it->symbol.code()==code && it->type==type) return &*it;
  }
  return nullptr;
}

void test_controller_marks_current_and_background_quote_priorities() {
  Queue q; StockController c(q); c.begin(config()); c.setWifiOnline(true); c.tick(1000,trading());
  const MarketRequest* current=find(q,"600519",MarketRequestType::QUOTE);
  const MarketRequest* background=find(q,"000001",MarketRequestType::QUOTE);
  TEST_ASSERT_NOT_NULL(current);
  TEST_ASSERT_NOT_NULL(background);
  TEST_ASSERT_EQUAL(MarketRequestPriority::CURRENT_QUOTE,current->priority);
  TEST_ASSERT_EQUAL(MarketRequestPriority::BACKGROUND_QUOTE,background->priority);
  TEST_ASSERT_EQUAL_UINT32(1000,current->createdMs);
  TEST_ASSERT_EQUAL_UINT32(1000,current->notBeforeMs);
  TEST_ASSERT_EQUAL_UINT8(1,current->attempt);
}

void test_cancelled_result_releases_controller_outstanding() {
  Queue q; StockController c(q); c.begin(config()); c.setWifiOnline(true); c.tick(1000,trading());
  const MarketRequest* firstPtr=find(q,"600519",MarketRequestType::QUOTE);
  TEST_ASSERT_NOT_NULL(firstPtr);
  const MarketRequest first=*firstPtr;
  MarketResult cancelled; cancelled.requestId=first.requestId; cancelled.type=first.type;
  cancelled.provider=first.provider; cancelled.error=ProviderError::CANCELLED;
  q.results.push_back(cancelled); c.consumeMarketResults();
  const size_t before=q.requests.size();
  c.tick(6000,trading());
  TEST_ASSERT_TRUE(q.requests.size()>before);
  TEST_ASSERT_NOT_NULL(find(q,"600519",MarketRequestType::QUOTE));
}

int main(){
  UNITY_BEGIN();
  RUN_TEST(test_controller_marks_current_and_background_quote_priorities);
  RUN_TEST(test_cancelled_result_releases_controller_outstanding);
  return UNITY_END();
}
