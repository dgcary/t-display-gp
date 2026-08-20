#include <unity.h>

#include <deque>

#include "CryptoController.h"

void setUp() {}
void tearDown() {}

class FakeQueue final : public IAppDataQueue {
 public:
  bool enqueue(const AppDataRequest& request) override {
    if (!acceptEnqueue) return false;
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
  bool acceptEnqueue = true;
  std::deque<AppDataRequest> requests;
  std::deque<AppDataResult> results;
};

CryptoSnapshot cryptoSnapshot(double btc) {
  CryptoSnapshot s;
  s.quotes[0] = {btc, 1.0, 100};
  s.quotes[1] = {3000.0, -2.0, 100};
  s.quotes[2] = {180.0, 3.0, 100};
  return s;
}

void test_crypto_schedules_only_when_active_and_online() {
  FakeQueue queue;
  CryptoController controller(queue);
  controller.begin();
  controller.setWifiOnline(true);
  controller.tick(10);
  TEST_ASSERT_TRUE(queue.requests.empty());
  controller.setActive(true);
  controller.tick(20);
  TEST_ASSERT_EQUAL_UINT32(1, queue.requests.size());
  TEST_ASSERT_EQUAL(AppDataRequestType::CRYPTO, queue.requests.front().type);
}

void test_crypto_success_caches_and_waits_sixty_seconds() {
  FakeQueue queue;
  CryptoController controller(queue);
  controller.begin();
  controller.setWifiOnline(true);
  controller.setActive(true);
  controller.tick(100);
  AppDataResult ok;
  ok.requestId = queue.requests.back().requestId;
  ok.type = AppDataRequestType::CRYPTO;
  ok.cryptoError = CryptoError::NONE;
  ok.crypto = cryptoSnapshot(70000.0);
  ok.completedMs = 200;
  queue.results.push_back(ok);
  controller.tick(200);
  TEST_ASSERT_TRUE(controller.viewModel().hasData);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 70000.0, controller.viewModel().snapshot.quotes[0].priceUsd);
  controller.tick(100 + 60000U - 1U);
  TEST_ASSERT_EQUAL_UINT32(1, queue.requests.size());
  controller.tick(100 + 60000U);
  TEST_ASSERT_EQUAL_UINT32(2, queue.requests.size());
}

void test_crypto_failure_preserves_cache_and_does_not_tight_retry() {
  FakeQueue queue;
  CryptoController controller(queue);
  controller.begin();
  controller.setWifiOnline(true);
  controller.setActive(true);
  controller.tick(1);
  AppDataResult ok;
  ok.requestId = queue.requests.back().requestId;
  ok.type = AppDataRequestType::CRYPTO;
  ok.cryptoError = CryptoError::NONE;
  ok.crypto = cryptoSnapshot(65000.0);
  ok.completedMs = 2;
  queue.results.push_back(ok);
  controller.tick(2);
  controller.tick(60001);
  AppDataResult fail;
  fail.requestId = queue.requests.back().requestId;
  fail.type = AppDataRequestType::CRYPTO;
  fail.cryptoError = CryptoError::NETWORK;
  fail.completedMs = 60002;
  queue.results.push_back(fail);
  controller.tick(60002);
  TEST_ASSERT_TRUE(controller.viewModel().hasData);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 65000.0, controller.viewModel().snapshot.quotes[0].priceUsd);
  TEST_ASSERT_EQUAL(CryptoError::NETWORK, controller.viewModel().error);
  controller.tick(61000);
  TEST_ASSERT_EQUAL_UINT32(2, queue.requests.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crypto_schedules_only_when_active_and_online);
  RUN_TEST(test_crypto_success_caches_and_waits_sixty_seconds);
  RUN_TEST(test_crypto_failure_preserves_cache_and_does_not_tight_retry);
  return UNITY_END();
}
