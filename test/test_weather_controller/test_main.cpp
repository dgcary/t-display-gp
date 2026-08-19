#include <unity.h>

#include <deque>

#include "WeatherController.h"

void setUp() {}
void tearDown() {}

class FakeAppDataQueue final : public IAppDataQueue {
 public:
  bool enqueue(const AppDataRequest& request) override {
    if (!acceptEnqueue) return false;
    requests.push_back(request);
    return true;
  }
  bool tryReceive(AppDataResult& result) override {
    if (results.empty()) return false;
    result = results.front();
    results.pop_front();
    return true;
  }
  bool acceptEnqueue = true;
  std::deque<AppDataRequest> requests;
  std::deque<AppDataResult> results;
};

AppConfig weatherConfig() {
  AppConfig config = AppConfig::defaults();
  config.stocks = {
      {StockSymbol::parse("600519"), "贵州茅台"},
      {StockSymbol::parse("000001"), "平安银行"},
      {StockSymbol::parse("300750"), "宁德时代"},
  };
  config.weather.enabled = true;
  config.weather.refreshMinutes = 15;
  config.location.displayName = "昆山";
  config.location.latitudeE6 = 31385000;
  config.location.longitudeE6 = 120980000;
  return config;
}

WeatherSnapshot snapshot(float temperature) {
  WeatherSnapshot s;
  s.currentTemp = temperature;
  s.apparentTemp = temperature + 2.0f;
  s.humidityPercent = 60;
  s.windSpeed = 8.0f;
  s.precipitationProbabilityPercent = 10;
  s.weatherCode = 1;
  s.today = {33.0f, 26.0f, 1};
  s.tomorrow = {32.0f, 25.0f, 2};
  s.dayAfter = {31.0f, 24.0f, 3};
  s.updatedEpochSeconds = 1787108400ULL;
  return s;
}

void test_first_active_online_tick_enqueues_weather_request() {
  FakeAppDataQueue queue;
  WeatherController controller(queue);
  controller.begin(weatherConfig());
  controller.setWifiOnline(true);
  controller.setActive(true);
  controller.tick(100);

  TEST_ASSERT_EQUAL_UINT32(1, queue.requests.size());
  TEST_ASSERT_EQUAL(AppDataRequestType::WEATHER, queue.requests.front().type);
  TEST_ASSERT_EQUAL_STRING("昆山", queue.requests.front().location.displayName.c_str());
  TEST_ASSERT_TRUE(controller.viewModel().requestInFlight);
}

void test_success_updates_cache_and_waits_for_refresh_interval() {
  FakeAppDataQueue queue;
  WeatherController controller(queue);
  controller.begin(weatherConfig());
  controller.setWifiOnline(true);
  controller.setActive(true);
  controller.tick(100);
  const uint32_t id = queue.requests.back().requestId;

  AppDataResult result;
  result.requestId = id;
  result.type = AppDataRequestType::WEATHER;
  result.error = WeatherError::NONE;
  result.weather = snapshot(31.2f);
  queue.results.push_back(result);
  controller.tick(200);

  TEST_ASSERT_TRUE(controller.viewModel().hasData);
  TEST_ASSERT_FALSE(controller.viewModel().requestInFlight);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 31.2f, controller.viewModel().weather.currentTemp);
  TEST_ASSERT_EQUAL(WeatherError::NONE, controller.viewModel().error);

  controller.tick(100 + 15u * 60u * 1000u - 1u);
  TEST_ASSERT_EQUAL_UINT32(1, queue.requests.size());
  controller.tick(100 + 15u * 60u * 1000u);
  TEST_ASSERT_EQUAL_UINT32(2, queue.requests.size());
}

void test_failed_refresh_preserves_last_valid_cache_and_does_not_retry_immediately() {
  FakeAppDataQueue queue;
  WeatherController controller(queue);
  controller.begin(weatherConfig());
  controller.setWifiOnline(true);
  controller.setActive(true);
  controller.tick(10);

  AppDataResult ok;
  ok.requestId = queue.requests.back().requestId;
  ok.type = AppDataRequestType::WEATHER;
  ok.error = WeatherError::NONE;
  ok.weather = snapshot(30.0f);
  queue.results.push_back(ok);
  controller.tick(20);

  const uint32_t refreshAt = 10 + 15u * 60u * 1000u;
  controller.tick(refreshAt);
  TEST_ASSERT_EQUAL_UINT32(2, queue.requests.size());

  AppDataResult failed;
  failed.requestId = queue.requests.back().requestId;
  failed.type = AppDataRequestType::WEATHER;
  failed.error = WeatherError::NETWORK;
  queue.results.push_back(failed);
  controller.tick(refreshAt + 1);

  TEST_ASSERT_TRUE(controller.viewModel().hasData);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, controller.viewModel().weather.currentTemp);
  TEST_ASSERT_EQUAL(WeatherError::NETWORK, controller.viewModel().error);
  TEST_ASSERT_EQUAL_UINT32(2, queue.requests.size());

  controller.tick(refreshAt + 1000);
  TEST_ASSERT_EQUAL_UINT32(2, queue.requests.size());
  controller.tick(refreshAt + 15u * 60u * 1000u);
  TEST_ASSERT_EQUAL_UINT32(3, queue.requests.size());
}

void test_offline_or_inactive_does_not_schedule() {
  FakeAppDataQueue queue;
  WeatherController controller(queue);
  controller.begin(weatherConfig());
  controller.setActive(true);
  controller.setWifiOnline(false);
  controller.tick(100);
  TEST_ASSERT_TRUE(queue.requests.empty());

  controller.setWifiOnline(true);
  controller.setActive(false);
  controller.tick(200);
  TEST_ASSERT_TRUE(queue.requests.empty());
}

void test_disabled_weather_reports_not_configured_and_does_not_schedule() {
  FakeAppDataQueue queue;
  AppConfig config = weatherConfig();
  config.weather.enabled = false;
  config.location = {};
  WeatherController controller(queue);
  controller.begin(config);
  controller.setWifiOnline(true);
  controller.setActive(true);
  controller.tick(100);

  TEST_ASSERT_FALSE(controller.viewModel().configured);
  TEST_ASSERT_TRUE(queue.requests.empty());
}

void test_stale_flag_uses_last_success_and_wrap_safe_elapsed() {
  FakeAppDataQueue queue;
  WeatherController controller(queue);
  controller.begin(weatherConfig());
  controller.setWifiOnline(true);
  controller.setActive(true);
  const uint32_t start = 0xFFF00000u;
  controller.tick(start);

  AppDataResult ok;
  ok.requestId = queue.requests.back().requestId;
  ok.type = AppDataRequestType::WEATHER;
  ok.error = WeatherError::NONE;
  ok.weather = snapshot(28.0f);
  queue.results.push_back(ok);
  controller.tick(start + 1u);

  controller.tick(start + 30u * 60u * 1000u + 2u);
  TEST_ASSERT_TRUE(controller.viewModel().stale);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_active_online_tick_enqueues_weather_request);
  RUN_TEST(test_success_updates_cache_and_waits_for_refresh_interval);
  RUN_TEST(test_failed_refresh_preserves_last_valid_cache_and_does_not_retry_immediately);
  RUN_TEST(test_offline_or_inactive_does_not_schedule);
  RUN_TEST(test_disabled_weather_reports_not_configured_and_does_not_schedule);
  RUN_TEST(test_stale_flag_uses_last_success_and_wrap_safe_elapsed);
  return UNITY_END();
}
