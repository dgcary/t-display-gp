#include <unity.h>

#include <deque>
#include <vector>

#include "StockController.h"

void setUp() {}
void tearDown() {}

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

AppConfig config1() { return config3(); }

LocalDateTime trading(){ return {2026,8,11,10,0,0,2}; }

const MarketRequest* lastRequest(const FakeQueue& q, const char* code, MarketRequestType type){
  for(auto it=q.requests.rbegin();it!=q.requests.rend();++it) if(it->symbol.code()==code && it->type==type) return &*it;
  return nullptr;
}

void pushQuoteSuccess(FakeQueue& q, const MarketRequest& req, double last=1410.25) {
  MarketResult r; r.requestId=req.requestId; r.type=req.type; r.provider=req.provider; r.error=ProviderError::NONE;
  r.quote.symbol=req.symbol; r.quote.name="贵州茅台"; r.quote.last=last; r.quote.prevClose=1400; r.quote.open=1401; r.quote.provider=req.provider;
  q.results.push_back(std::move(r));
}

void pushIntradaySuccess(FakeQueue& q, const MarketRequest& req) {
  MarketResult r; r.requestId=req.requestId; r.type=req.type; r.provider=req.provider; r.error=ProviderError::NONE;
  r.intraday={{570,1401.0f,1401.0f,10},{600,1410.0f,1405.0f,20}};
  q.results.push_back(std::move(r));
}

void pushFailure(FakeQueue& q, const MarketRequest& req, ProviderError error) {
  MarketResult r; r.requestId=req.requestId; r.type=req.type; r.provider=req.provider; r.error=error;
  q.results.push_back(std::move(r));
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

void test_intraday_failure_does_not_poison_quote_health_or_clear_cache() {
  FakeQueue q; StockController c(q); c.begin(config1()); c.setWifiOnline(true); c.tick(1000,trading());
  const MarketRequest* quotePtr=lastRequest(q,"600519",MarketRequestType::QUOTE);
  const MarketRequest* trendPtr=lastRequest(q,"600519",MarketRequestType::INTRADAY);
  TEST_ASSERT_NOT_NULL(quotePtr); TEST_ASSERT_NOT_NULL(trendPtr);
  const MarketRequest quote0=*quotePtr; const MarketRequest trend0=*trendPtr;
  pushQuoteSuccess(q,quote0); pushIntradaySuccess(q,trend0); c.consumeMarketResults();

  q.requests.clear(); c.tick(61000,trading());
  trendPtr=lastRequest(q,"600519",MarketRequestType::INTRADAY); TEST_ASSERT_NOT_NULL(trendPtr);
  const MarketRequest trend1=*trendPtr;
  pushFailure(q,trend1,ProviderError::NETWORK); c.consumeMarketResults();

  TEST_ASSERT_TRUE(c.viewModel().hasQuote);
  TEST_ASSERT_TRUE(c.viewModel().hasIntraday);
  TEST_ASSERT_EQUAL(ProviderError::NONE,c.viewModel().quoteError);
  TEST_ASSERT_EQUAL(ProviderError::NETWORK,c.viewModel().intradayError);
  TEST_ASSERT_FALSE(c.viewModel().intradayDelayed);
  TEST_ASSERT_EQUAL_STRING("",c.viewModel().errorBadge.c_str());
}

void test_quote_success_does_not_clear_intraday_error() {
  FakeQueue q; StockController c(q); c.begin(config1()); c.setWifiOnline(true); c.tick(1000,trading());
  const MarketRequest* quotePtr=lastRequest(q,"600519",MarketRequestType::QUOTE);
  const MarketRequest* trendPtr=lastRequest(q,"600519",MarketRequestType::INTRADAY);
  TEST_ASSERT_NOT_NULL(quotePtr); TEST_ASSERT_NOT_NULL(trendPtr);
  const MarketRequest quote0=*quotePtr; const MarketRequest trend0=*trendPtr;
  pushQuoteSuccess(q,quote0); pushIntradaySuccess(q,trend0); c.consumeMarketResults();

  q.requests.clear(); c.tick(61000,trading());
  quotePtr=lastRequest(q,"600519",MarketRequestType::QUOTE);
  trendPtr=lastRequest(q,"600519",MarketRequestType::INTRADAY);
  TEST_ASSERT_NOT_NULL(quotePtr); TEST_ASSERT_NOT_NULL(trendPtr);
  const MarketRequest quote1=*quotePtr; const MarketRequest trend1=*trendPtr;
  pushFailure(q,trend1,ProviderError::NETWORK); pushQuoteSuccess(q,quote1,1411.0); c.consumeMarketResults();

  TEST_ASSERT_EQUAL(ProviderError::NONE,c.viewModel().quoteError);
  TEST_ASSERT_EQUAL(ProviderError::NETWORK,c.viewModel().intradayError);
}

void test_intraday_success_does_not_clear_quote_error() {
  FakeQueue q; StockController c(q); c.begin(config1()); c.setWifiOnline(true); c.tick(1000,trading());
  const MarketRequest* quotePtr=lastRequest(q,"600519",MarketRequestType::QUOTE);
  const MarketRequest* trendPtr=lastRequest(q,"600519",MarketRequestType::INTRADAY);
  TEST_ASSERT_NOT_NULL(quotePtr); TEST_ASSERT_NOT_NULL(trendPtr);
  const MarketRequest quote0=*quotePtr; const MarketRequest trend0=*trendPtr;
  pushQuoteSuccess(q,quote0); pushIntradaySuccess(q,trend0); c.consumeMarketResults();

  q.requests.clear(); c.tick(61000,trading());
  quotePtr=lastRequest(q,"600519",MarketRequestType::QUOTE);
  trendPtr=lastRequest(q,"600519",MarketRequestType::INTRADAY);
  TEST_ASSERT_NOT_NULL(quotePtr); TEST_ASSERT_NOT_NULL(trendPtr);
  const MarketRequest quote1=*quotePtr; const MarketRequest trend1=*trendPtr;
  pushFailure(q,quote1,ProviderError::NETWORK); pushIntradaySuccess(q,trend1); c.consumeMarketResults();

  TEST_ASSERT_EQUAL(ProviderError::NETWORK,c.viewModel().quoteError);
  TEST_ASSERT_EQUAL(ProviderError::NONE,c.viewModel().intradayError);
}

void test_channel_ages_drive_independent_delay_flags() {
  FakeQueue q; StockController c(q); c.begin(config1()); c.setWifiOnline(true); c.tick(1000,trading());
  const MarketRequest* quotePtr=lastRequest(q,"600519",MarketRequestType::QUOTE);
  const MarketRequest* trendPtr=lastRequest(q,"600519",MarketRequestType::INTRADAY);
  TEST_ASSERT_NOT_NULL(quotePtr); TEST_ASSERT_NOT_NULL(trendPtr);
  const MarketRequest quote0=*quotePtr; const MarketRequest trend0=*trendPtr;
  pushQuoteSuccess(q,quote0); pushIntradaySuccess(q,trend0); c.consumeMarketResults();

  c.tick(16000,trading());
  TEST_ASSERT_EQUAL_UINT32(15,c.viewModel().quoteAgeSeconds);
  TEST_ASSERT_TRUE(c.viewModel().quoteDelayed);
  TEST_ASSERT_EQUAL_STRING("报价延迟",c.viewModel().errorBadge.c_str());

  c.tick(181000,trading());
  TEST_ASSERT_EQUAL_UINT32(180,c.viewModel().intradayAgeSeconds);
  TEST_ASSERT_TRUE(c.viewModel().intradayDelayed);
}

void test_idle_tick_does_not_mark_view_dirty() {
  FakeQueue q; StockController c(q); c.begin(config3()); c.setWifiOnline(false);
  LocalDateTime t{2026,8,11,10,0,0,2};
  c.tick(1000,t);
  (void)c.takeDirtyFlag();
  c.tick(1000,t);
  TEST_ASSERT_FALSE(c.takeDirtyFlag());
}

int main(){
  UNITY_BEGIN();
  RUN_TEST(test_button_switch_wraps_and_requests_stale_cache);
  RUN_TEST(test_trading_cycle_requests_current_and_only_one_other_quote);
  RUN_TEST(test_three_primary_failures_switch_quotes_to_tencent);
  RUN_TEST(test_offline_keeps_cached_quote_and_sets_badge);
  RUN_TEST(test_intraday_failure_does_not_poison_quote_health_or_clear_cache);
  RUN_TEST(test_quote_success_does_not_clear_intraday_error);
  RUN_TEST(test_intraday_success_does_not_clear_quote_error);
  RUN_TEST(test_channel_ages_drive_independent_delay_flags);
  RUN_TEST(test_idle_tick_does_not_mark_view_dirty);
  return UNITY_END();
}
