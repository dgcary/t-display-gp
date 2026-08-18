#include <unity.h>

#include <deque>
#include <vector>

#include "StockController.h"

void setUp() {}
void tearDown() {}

class AcceptanceQueue : public IMarketDataQueue {
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

AppConfig acceptanceConfig() {
  AppConfig config;
  config.quoteRefreshSec = 5;
  config.stocks = {
      {StockSymbol::parse("600519"), "贵州茅台"},
      {StockSymbol::parse("300750"), "宁德时代"},
      {StockSymbol::parse("920047"), "诺思兰德"},
  };
  return config;
}

LocalDateTime am() { return {2026, 8, 11, 10, 0, 0, 2}; }
LocalDateTime lunch() { return {2026, 8, 11, 12, 0, 0, 2}; }

const MarketRequest* latest(const AcceptanceQueue& queue, const char* code, MarketRequestType type) {
  for (auto it = queue.requests.rbegin(); it != queue.requests.rend(); ++it) {
    if (it->symbol.code() == code && it->type == type) return &*it;
  }
  return nullptr;
}

size_t countRequests(const AcceptanceQueue& queue, const char* code, MarketRequestType type) {
  size_t count = 0;
  for (const auto& request : queue.requests) {
    if (request.symbol.code() == code && request.type == type) ++count;
  }
  return count;
}

void succeedQuote(AcceptanceQueue& queue, const MarketRequest& request, uint64_t epoch = 1786413600ULL) {
  MarketResult result;
  result.requestId = request.requestId;
  result.type = request.type;
  result.error = ProviderError::NONE;
  result.quote.symbol = request.symbol;
  result.quote.name = request.symbol.code();
  result.quote.last = 100;
  result.quote.prevClose = 99;
  result.quote.open = 99.5;
  result.quote.high = 101;
  result.quote.low = 98;
  result.quote.epochSeconds = epoch;
  result.quote.provider = request.provider;
  queue.results.push_back(std::move(result));
}

void succeedIntraday(AcceptanceQueue& queue, const MarketRequest& request) {
  MarketResult result;
  result.requestId = request.requestId;
  result.type = request.type;
  result.error = ProviderError::NONE;
  result.intraday = {{570, 99.5f, 99.5f, 10}, {600, 100.0f, 99.8f, 20}};
  queue.results.push_back(std::move(result));
}

void test_trading_quote_refresh_obeys_five_second_interval() {
  AcceptanceQueue queue;
  StockController controller(queue);
  controller.begin(acceptanceConfig());
  controller.setWifiOnline(true);
  controller.tick(1000, am());
  const MarketRequest* first = latest(queue, "600519", MarketRequestType::QUOTE);
  TEST_ASSERT_TRUE(first != nullptr);
  const MarketRequest firstCopy = *first;
  succeedQuote(queue, firstCopy);
  controller.consumeMarketResults();
  TEST_ASSERT_EQUAL_UINT32(1, countRequests(queue, "600519", MarketRequestType::QUOTE));
  controller.tick(5999, am());
  TEST_ASSERT_EQUAL_UINT32(1, countRequests(queue, "600519", MarketRequestType::QUOTE));
  controller.tick(6000, am());
  TEST_ASSERT_EQUAL_UINT32(2, countRequests(queue, "600519", MarketRequestType::QUOTE));
}

void test_intraday_refresh_is_independent_at_sixty_seconds() {
  AcceptanceQueue queue;
  StockController controller(queue);
  controller.begin(acceptanceConfig());
  controller.setWifiOnline(true);
  controller.tick(1000, am());
  const MarketRequest* first = latest(queue, "600519", MarketRequestType::INTRADAY);
  TEST_ASSERT_TRUE(first != nullptr);
  const MarketRequest firstCopy = *first;
  succeedIntraday(queue, firstCopy);
  controller.consumeMarketResults();
  controller.tick(60999, am());
  TEST_ASSERT_EQUAL_UINT32(1, countRequests(queue, "600519", MarketRequestType::INTRADAY));
  controller.tick(61000, am());
  TEST_ASSERT_EQUAL_UINT32(2, countRequests(queue, "600519", MarketRequestType::INTRADAY));
}

void test_lunch_stops_high_frequency_quote_polling() {
  AcceptanceQueue queue;
  StockController controller(queue);
  controller.begin(acceptanceConfig());
  controller.setWifiOnline(true);
  controller.tick(1000, lunch());
  const MarketRequest* first = latest(queue, "600519", MarketRequestType::QUOTE);
  TEST_ASSERT_TRUE(first != nullptr);
  const MarketRequest firstCopy = *first;
  succeedQuote(queue, firstCopy);
  controller.consumeMarketResults();
  controller.tick(6000, lunch());
  TEST_ASSERT_EQUAL_UINT32(1, countRequests(queue, "600519", MarketRequestType::QUOTE));
  controller.tick(61000, lunch());
  TEST_ASSERT_EQUAL_UINT32(2, countRequests(queue, "600519", MarketRequestType::QUOTE));
}

void test_close_retains_cache_and_next_session_resumes() {
  AcceptanceQueue queue;
  StockController controller(queue);
  controller.begin(acceptanceConfig());
  controller.setWifiOnline(true);
  controller.tick(1000, am());
  const MarketRequest quoteRequest = *latest(queue, "600519", MarketRequestType::QUOTE);
  const MarketRequest intradayRequest = *latest(queue, "600519", MarketRequestType::INTRADAY);
  succeedQuote(queue, quoteRequest);
  succeedIntraday(queue, intradayRequest);
  controller.consumeMarketResults();

  controller.tick(10000, {2026, 8, 11, 15, 5, 0, 2});
  TEST_ASSERT_EQUAL(MarketStatus::CLOSED, controller.viewModel().marketStatus);
  TEST_ASSERT_TRUE(controller.viewModel().hasQuote);
  TEST_ASSERT_TRUE(controller.viewModel().hasIntraday);

  const size_t before = countRequests(queue, "600519", MarketRequestType::QUOTE);
  controller.tick(20000, {2026, 8, 12, 9, 31, 0, 3});
  TEST_ASSERT_EQUAL(MarketStatus::TRADING_AM, controller.viewModel().marketStatus);
  TEST_ASSERT_TRUE(countRequests(queue, "600519", MarketRequestType::QUOTE) > before);
}

void test_stale_tencent_date_marks_weekday_as_non_trading() {
  AcceptanceQueue queue;
  StockController controller(queue);
  controller.begin(acceptanceConfig());
  controller.setWifiOnline(true);
  controller.tick(1000, am());
  const MarketRequest* request = latest(queue, "600519", MarketRequestType::QUOTE);
  TEST_ASSERT_TRUE(request != nullptr);
  const MarketRequest copy = *request;
  // 2026-08-10 15:00:00 China time: stale relative to local 2026-08-11.
  succeedQuote(queue, copy, 1786345200ULL);
  controller.consumeMarketResults();
  controller.tick(2000, am());
  TEST_ASSERT_EQUAL(MarketStatus::NON_TRADING_DAY, controller.viewModel().marketStatus);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_trading_quote_refresh_obeys_five_second_interval);
  RUN_TEST(test_intraday_refresh_is_independent_at_sixty_seconds);
  RUN_TEST(test_lunch_stops_high_frequency_quote_polling);
  RUN_TEST(test_close_retains_cache_and_next_session_resumes);
  RUN_TEST(test_stale_tencent_date_marks_weekday_as_non_trading);
  return UNITY_END();
}
