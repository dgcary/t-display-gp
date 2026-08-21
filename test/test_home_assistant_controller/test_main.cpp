#include <unity.h>

#include <deque>

#include "HomeAssistantController.h"

void setUp() {}
void tearDown() {}

class FakeQueue final : public IAppDataQueue {
 public:
  bool enqueue(const AppDataRequest& request) override {
    requests.push_back(request);
    return true;
  }
  bool tryReceive(AppDataRequestType type, AppDataResult& result) override {
    for (auto it = results.begin(); it != results.end(); ++it) {
      if (it->type != type) continue;
      result = *it;
      results.erase(it);
      return true;
    }
    return false;
  }
  std::deque<AppDataRequest> requests;
  std::deque<AppDataResult> results;
};

HomeAssistantConfig haConfig() {
  HomeAssistantConfig c;
  c.enabled = true;
  c.baseUrl = "https://ha.example.test:8123";
  c.token = "token";
  c.caCert = "-----BEGIN CERTIFICATE-----\nX\n-----END CERTIFICATE-----\n";
  c.refreshSeconds = 30;
  c.entityCount = 2;
  c.entities[0] = {"sensor.temperature", "温度"};
  c.entities[1] = {"binary_sensor.door", "门"};
  return c;
}

AppDataResult successFor(const AppDataRequest& request, const char* state, uint32_t completed) {
  AppDataResult result;
  result.requestId = request.requestId;
  result.type = AppDataRequestType::HOME_ASSISTANT;
  result.entityIndex = request.entityIndex;
  result.homeAssistantError = HomeAssistantError::NONE;
  result.homeAssistant.entityId = request.entityIndex == 0 ? "sensor.temperature" : "binary_sensor.door";
  result.homeAssistant.state = state;
  result.completedMs = completed;
  return result;
}

void test_home_assistant_fetches_entities_sequentially() {
  FakeQueue queue;
  HomeAssistantController controller(queue);
  controller.begin(haConfig());
  controller.setWifiOnline(true);
  controller.setActive(true);
  controller.tick(100);
  TEST_ASSERT_EQUAL_UINT32(1, queue.requests.size());
  TEST_ASSERT_EQUAL_UINT8(0, queue.requests.back().entityIndex);
  queue.results.push_back(successFor(queue.requests.back(), "26.4", 200));
  controller.tick(200);
  TEST_ASSERT_EQUAL_UINT32(2, queue.requests.size());
  TEST_ASSERT_EQUAL_UINT8(1, queue.requests.back().entityIndex);
  queue.results.push_back(successFor(queue.requests.back(), "off", 300));
  controller.tick(300);
  TEST_ASSERT_FALSE(controller.viewModel().requestInFlight);
  TEST_ASSERT_TRUE(controller.viewModel().entities[0].hasData);
  TEST_ASSERT_TRUE(controller.viewModel().entities[1].hasData);
  TEST_ASSERT_EQUAL_STRING("26.4", controller.viewModel().entities[0].state.c_str());
}

void test_home_assistant_failure_preserves_previous_entity_cache() {
  FakeQueue queue;
  HomeAssistantController controller(queue);
  HomeAssistantConfig c = haConfig();
  c.entityCount = 1;
  controller.begin(c);
  controller.setWifiOnline(true);
  controller.setActive(true);
  controller.tick(10);
  queue.results.push_back(successFor(queue.requests.back(), "25.0", 20));
  controller.tick(20);
  controller.tick(30010);
  AppDataResult failed;
  failed.requestId = queue.requests.back().requestId;
  failed.type = AppDataRequestType::HOME_ASSISTANT;
  failed.entityIndex = 0;
  failed.homeAssistantError = HomeAssistantError::NETWORK;
  failed.completedMs = 30020;
  queue.results.push_back(failed);
  controller.tick(30020);
  TEST_ASSERT_TRUE(controller.viewModel().entities[0].hasData);
  TEST_ASSERT_EQUAL_STRING("25.0", controller.viewModel().entities[0].state.c_str());
  TEST_ASSERT_EQUAL(HomeAssistantError::NETWORK, controller.viewModel().entities[0].error);
}

void test_home_assistant_inactive_or_offline_does_not_schedule() {
  FakeQueue queue;
  HomeAssistantController controller(queue);
  controller.begin(haConfig());
  controller.setWifiOnline(true);
  controller.tick(100);
  TEST_ASSERT_TRUE(queue.requests.empty());
  controller.setActive(true);
  controller.setWifiOnline(false);
  controller.tick(200);
  TEST_ASSERT_TRUE(queue.requests.empty());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_home_assistant_fetches_entities_sequentially);
  RUN_TEST(test_home_assistant_failure_preserves_previous_entity_cache);
  RUN_TEST(test_home_assistant_inactive_or_offline_does_not_schedule);
  return UNITY_END();
}
