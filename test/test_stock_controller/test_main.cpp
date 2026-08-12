#include <unity.h>

#include <deque>
#include <vector>

#include "StockController.h"

class FakeQueue : public IMarketDataQueue {
 public:
  std::vector<MarketRequest> requests;
  std::deque<MarketResult> results;
  bool accept = true;
  bool enqueue(const MarketRequest& r) override { if(!accept) return false; requests.push_back(r); return true; }
  bool tryReceive(MarketResult& r) override { if(results.empty()) return false; r=std::move(results.front()); results.pop_front(); return true; }
};

AppConfig config3() {
  AppConfig c; c.quoteRefreshSec=5;
  c.stocks={{StockSymbol::parse("600519"),"茅台"},{StockSymbol::parse("000001"),"平安"},{StockSymbol::parse("300750"),"宁德"}};
  return c;
}
LocalDateTime trading(){ return {2026,8,11,10,0,0,2}; }

const MarketRequest* lastRequest(const FakeQueue& q, const char* code, MarketRequestType type){
  for(auto it=q.requests.rbegin();it!=q.requests.rend();++it) if(it->symbol.code()==code && it->type==type) return &*it;
  return nullptr;
}

void test_button_switch_wraps_and_requests_stale_cache() {
  FakeQueue q; StockController c(q); c.begin(config3()); c.setWifiOnline(true); c.tick(1000,trading()); q.requests.clear();
  c.onButton(ButtonEvent::PREVIOUS);
  TEST_ASSERT_EQUAL_UINT32(2,c.viewModel().index);
  TEST_ASSERT_TRUE(lastRequest(q,"300750",MarketRequestType::QUOTE) != nullptr);
  c.onButton(ButtonEvent::NEXT);
  TEST_ASSERT_EQUAL_UINT32(0,c.viewModel().index);
}

void test_trading_cycle_requests_current_and_only_one_other_quote() {
  FakeQueue q; StockController c(q); c.begin(config3()); c.setWifiOnline(true);
  c.tick(5000,trading());
  int quoteCount=0; for(const auto& r:q.requests) if(r.type==MarketRequestType::QUOTE) quoteCount++;
  TEST_ASSERT_EQUAL(2, quoteCount);
  TEST_ASSERT_TRUE(lastRequest(q,"600519",MarketRequestType::QUOTE) != nullptr);
}

void test_three_primary_failures_switch_quotes_to_tencent() {
  FakeQueue q; StockController c(q); c.begin(config3()); c.setWifiOnline(true);
  uint32_t now=5000;
  for(int i=0;i<3;i++){
    c.tick(now,trading());
    const MarketRequest* req=lastRequest(q,"600519",MarketRequestType::QUOTE);
    TEST_ASSERT_TRUE(req != nullptr); TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, req->provider);
    MarketResult r; r.requestId=req->requestId; r.type=MarketRequestType::QUOTE; r.error=ProviderError::NETWORK; q.results.push_back(r);
    c.consumeMarketResults(); now+=5000;
  }
  c.tick(now,trading());
  const MarketRequest* fallback=lastRequest(q,"600519",MarketRequestType::QUOTE);
  TEST_ASSERT_TRUE(fallback != nullptr); TEST_ASSERT_EQUAL(ProviderId::TENCENT, fallback->provider);
}

void test_offline_keeps_cached_quote_and_sets_badge() {
  FakeQueue q; StockController c(q); c.begin(config3()); c.setWifiOnline(true); c.tick(5000,trading());
  const MarketRequest* req=lastRequest(q,"600519",MarketRequestType::QUOTE); TEST_ASSERT_TRUE(req != nullptr);
  MarketResult r; r.requestId=req->requestId; r.type=MarketRequestType::QUOTE; r.error=ProviderError::NONE;
  r.quote.symbol=StockSymbol::parse("600519"); r.quote.name="贵州茅台"; r.quote.last=1410.25; r.quote.prevClose=1400; r.quote.provider=ProviderId::EAST_MONEY;
  q.results.push_back(r); c.consumeMarketResults();
  c.tick(10000,trading()); c.setWifiOnline(false);
  TEST_ASSERT_TRUE(c.viewModel().hasQuote); TEST_ASSERT_DOUBLE_WITHIN(0.001,1410.25,c.viewModel().quote->last);
  TEST_ASSERT_EQUAL_STRING("离线",c.viewModel().errorBadge.c_str());
}


void test_idle_tick_does_not_mark_view_dirty() {
  FakeQueue q; StockController c(q); c.begin(config3()); c.setWifiOnline(false);
  LocalDateTime t{2026,8,11,10,0,0,2};
  c.tick(1000,t);
  (void)c.takeDirtyFlag();
  c.tick(1000,t);
  TEST_ASSERT_FALSE(c.takeDirtyFlag());
}

int main(){UNITY_BEGIN();RUN_TEST(test_button_switch_wraps_and_requests_stale_cache);RUN_TEST(test_trading_cycle_requests_current_and_only_one_other_quote);RUN_TEST(test_three_primary_failures_switch_quotes_to_tencent);RUN_TEST(test_offline_keeps_cached_quote_and_sets_badge);RUN_TEST(test_idle_tick_does_not_mark_view_dirty);return UNITY_END();}
