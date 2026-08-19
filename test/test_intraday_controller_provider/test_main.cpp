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

  bool enqueue(const MarketRequest& request) override {
    requests.push_back(request);
    return true;
  }
  bool tryReceive(MarketResult& result) override {
    if (results.empty()) return false;
    result = std::move(results.front());
    results.pop_front();
    return true;
  }
};

AppConfig config() {
  AppConfig value;
  value.quoteRefreshSec = 5;
  value.stocks = {{StockSymbol::parse("600519"), "茅台"},
                  {StockSymbol::parse("000001"), "平安"},
                  {StockSymbol::parse("300750"), "宁德"}};
  return value;
}

LocalDateTime trading() { return {2026, 8, 11, 10, 0, 0, 2}; }

const MarketRequest* lastRequest(const FakeQueue& queue, MarketRequestType type) {
  for (auto it = queue.requests.rbegin(); it != queue.requests.rend(); ++it) {
    if (it->symbol.code() == "600519" && it->type == type) return &*it;
  }
  return nullptr;
}

void pushFailure(FakeQueue& queue, const MarketRequest& request) {
  MarketResult result;
  result.requestId = request.requestId;
  result.type = request.type;
  result.provider = request.provider;
  result.error = ProviderError::NETWORK;
  queue.results.push_back(result);
}

void test_failed_empty_intraday_waits_full_refresh_interval_before_new_cycle() {
  FakeQueue queue;
  StockController controller(queue);
  controller.begin(config());
  controller.setWifiOnline(true);
  controller.tick(5000, trading());

  const MarketRequest* first = lastRequest(queue, MarketRequestType::INTRADAY);
  TEST_ASSERT_NOT_NULL(first);
  pushFailure(queue, *first);
  controller.consumeMarketResults();

  queue.requests.clear();
  controller.tick(10000, trading());
  TEST_ASSERT_NULL(lastRequest(queue, MarketRequestType::INTRADAY));

  queue.requests.clear();
  controller.tick(64999, trading());
  TEST_ASSERT_NULL(lastRequest(queue, MarketRequestType::INTRADAY));

  queue.requests.clear();
  controller.tick(65000, trading());
  const MarketRequest* due = lastRequest(queue, MarketRequestType::INTRADAY);
  TEST_ASSERT_NOT_NULL(due);
  TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, due->provider);
}

void test_tencent_quote_failover_does_not_suppress_eastmoney_first_intraday_cycle() {
  FakeQueue queue;
  StockController controller(queue);
  controller.begin(config());
  controller.setWifiOnline(true);

  uint32_t now = 5000;
  for (int i = 0; i < 3; ++i) {
    controller.tick(now, trading());
    const MarketRequest* quote = lastRequest(queue, MarketRequestType::QUOTE);
    TEST_ASSERT_NOT_NULL(quote);
    TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, quote->provider);
    pushFailure(queue, *quote);
    if (i == 0) {
      const MarketRequest* intraday = lastRequest(queue, MarketRequestType::INTRADAY);
      TEST_ASSERT_NOT_NULL(intraday);
      pushFailure(queue, *intraday);
    }
    controller.consumeMarketResults();
    now += 5000;
  }

  queue.requests.clear();
  controller.tick(now, trading());
  const MarketRequest* quote = lastRequest(queue, MarketRequestType::QUOTE);
  TEST_ASSERT_NOT_NULL(quote);
  TEST_ASSERT_EQUAL(ProviderId::TENCENT, quote->provider);

  queue.requests.clear();
  controller.tick(65000, trading());
  const MarketRequest* intraday = lastRequest(queue, MarketRequestType::INTRADAY);
  TEST_ASSERT_NOT_NULL(intraday);
  TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, intraday->provider);
}

void test_tencent_intraday_success_records_chart_source_without_changing_quote_provider() {
  FakeQueue queue;
  StockController controller(queue);
  controller.begin(config());
  controller.setWifiOnline(true);
  controller.tick(1000, trading());

  const MarketRequest* intraday = lastRequest(queue, MarketRequestType::INTRADAY);
  TEST_ASSERT_NOT_NULL(intraday);

  MarketResult result;
  result.requestId = intraday->requestId;
  result.type = MarketRequestType::INTRADAY;
  result.provider = ProviderId::TENCENT;
  result.error = ProviderError::NONE;
  result.intraday = {{570, 1401.0f, 1401.0f, 10}, {571, 1402.0f, 1401.5f, 20}};
  queue.results.push_back(std::move(result));
  controller.consumeMarketResults();

  TEST_ASSERT_TRUE(controller.viewModel().hasIntraday);
  TEST_ASSERT_EQUAL(ProviderId::TENCENT, controller.viewModel().intradayProvider);
  TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, controller.viewModel().provider);
  TEST_ASSERT_EQUAL(ProviderError::NONE, controller.viewModel().intradayError);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_failed_empty_intraday_waits_full_refresh_interval_before_new_cycle);
  RUN_TEST(test_tencent_quote_failover_does_not_suppress_eastmoney_first_intraday_cycle);
  RUN_TEST(test_tencent_intraday_success_records_chart_source_without_changing_quote_provider);
  return UNITY_END();
}
