#include <unity.h>

#include <deque>
#include <vector>

#include "StockController.h"

void setUp() {}
void tearDown() {}

class HealthQueue final : public IMarketDataQueue {
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

const MarketRequest* latest(const HealthQueue& q, MarketRequestType type) {
  for (auto it=q.requests.rbegin(); it!=q.requests.rend(); ++it) {
    if (it->symbol.code()=="600519" && it->type==type) return &*it;
  }
  return nullptr;
}

void loadCache(HealthQueue& q, StockController& c) {
  c.begin(config()); c.setWifiOnline(true);
  c.tick(1000,{2026,8,11,10,0,0,2});
  const MarketRequest* quote=latest(q,MarketRequestType::QUOTE);
  const MarketRequest* trend=latest(q,MarketRequestType::INTRADAY);
  TEST_ASSERT_NOT_NULL(quote); TEST_ASSERT_NOT_NULL(trend);

  MarketResult qr; qr.requestId=quote->requestId; qr.type=quote->type; qr.error=ProviderError::NONE;
  qr.quote.symbol=quote->symbol; qr.quote.last=100; qr.quote.prevClose=99; qr.quote.open=99.5;
  q.results.push_back(qr);
  MarketResult tr; tr.requestId=trend->requestId; tr.type=trend->type; tr.error=ProviderError::NONE;
  tr.intraday={{570,99.5f,99.5f,10},{600,100.0f,99.8f,20}};
  q.results.push_back(tr);
  c.consumeMarketResults();
}

void test_lunch_does_not_report_quote_or_intraday_delay() {
  HealthQueue q; StockController c(q); loadCache(q,c);
  c.tick(400000,{2026,8,11,12,0,0,2});
  TEST_ASSERT_EQUAL(MarketStatus::LUNCH_BREAK,c.viewModel().marketStatus);
  TEST_ASSERT_FALSE(c.viewModel().quoteDelayed);
  TEST_ASSERT_FALSE(c.viewModel().intradayDelayed);
  TEST_ASSERT_EQUAL_STRING("",c.viewModel().errorBadge.c_str());
}

void test_closed_market_does_not_report_stale_data_as_delay() {
  HealthQueue q; StockController c(q); loadCache(q,c);
  c.tick(500000,{2026,8,11,15,5,0,2});
  TEST_ASSERT_EQUAL(MarketStatus::CLOSED,c.viewModel().marketStatus);
  TEST_ASSERT_FALSE(c.viewModel().quoteDelayed);
  TEST_ASSERT_FALSE(c.viewModel().intradayDelayed);
  TEST_ASSERT_EQUAL_STRING("",c.viewModel().errorBadge.c_str());
}

int main(){
  UNITY_BEGIN();
  RUN_TEST(test_lunch_does_not_report_quote_or_intraday_delay);
  RUN_TEST(test_closed_market_does_not_report_stale_data_as_delay);
  return UNITY_END();
}
