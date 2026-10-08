#include <unity.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>

#include "BambuMqttTransport.h"

namespace {
uint32_t clockMs;
uint32_t lastIdleMs;
uint32_t longestIdleGap;
uint32_t sleepCount;
void observeIdleGap() {
  const uint32_t gap = clockMs-lastIdleMs;
  if (gap > longestIdleGap) longestIdleGap = gap;
}
void connack(WiFiClientSecure& transport, uint32_t at, uint8_t rc = 0) {
  transport.incoming = {{at, 0x20}, {at, 0x02}, {at, 0x00}, {at, rc}};
}
}
unsigned long millis() { ++clockMs; observeIdleGap(); return clockMs; }
// ESP32 yield alone does not block a priority-1 worker for the priority-0 idle task.
void yield() { ++clockMs; observeIdleGap(); }
void vTaskDelay(unsigned ticks) {
  TEST_ASSERT_GREATER_THAN_UINT32(0, ticks);
  clockMs += ticks;
  observeIdleGap();
  lastIdleMs = clockMs;
  ++sleepCount;
}
void setUp() { clockMs=lastIdleMs=longestIdleGap=sleepCount=0; }
void tearDown() {}

void test_missing_connack_returns_timeout_without_starving_idle() {
  BambuMqttTransport transport;
  PubSubClient mqtt(transport);
  mqtt.setServer("test.invalid", 8883).setSocketTimeout(5);
  TEST_ASSERT_FALSE(mqtt.connect("test-client"));
  TEST_ASSERT_EQUAL_INT(-4, mqtt.state());
  TEST_ASSERT_UINT32_WITHIN(50, 5000, clockMs);
  TEST_ASSERT_FALSE(transport.socketOpen);
  TEST_ASSERT_LESS_THAN_UINT32(100, longestIdleGap);
}
void test_delayed_connack_connects_while_idle_keeps_running() {
  BambuMqttTransport transport;
  connack(transport, 3500);
  PubSubClient mqtt(transport);
  mqtt.setServer("test.invalid", 8883).setSocketTimeout(5);
  TEST_ASSERT_TRUE(mqtt.connect("test-client"));
  TEST_ASSERT_EQUAL_INT(0, mqtt.state());
  TEST_ASSERT_LESS_THAN_UINT32(100, longestIdleGap);
}
void test_fragmented_connack_readbyte_wait_also_allows_idle() {
  BambuMqttTransport transport;
  transport.incoming = {{10, 0x20}, {1000, 0x02}, {2000, 0x00}, {3000, 0x00}};
  PubSubClient mqtt(transport);
  mqtt.setServer("test.invalid", 8883).setSocketTimeout(5);
  TEST_ASSERT_TRUE(mqtt.connect("test-client"));
  TEST_ASSERT_LESS_THAN_UINT32(100, longestIdleGap);
}
void test_fragmented_live_publish_preserves_payload_and_yields() {
  BambuMqttTransport transport;
  connack(transport, 0);
  PubSubClient mqtt(transport);
  mqtt.setServer("test.invalid", 8883).setSocketTimeout(5);
  TEST_ASSERT_TRUE(mqtt.connect("test-client"));
  bool received = false;
  // Non-capturing callback below uses shared test-local state.
  static bool* callbackReceived;
  callbackReceived = &received;
  mqtt.setCallback([](char* topic, uint8_t* payload, unsigned int length) {
    TEST_ASSERT_EQUAL_STRING("t", topic);
    TEST_ASSERT_EQUAL_UINT32(2, length);
    TEST_ASSERT_EQUAL_MEMORY("OK", payload, 2);
    *callbackReceived = true;
  });
  transport.incoming.insert(transport.incoming.end(),
      {{0,0x30},{0,5},{0,0},{0,1},{0,'t'},{2000,'O'},{3000,'K'}});
  TEST_ASSERT_TRUE(mqtt.loop());
  TEST_ASSERT_TRUE(received);
  TEST_ASSERT_LESS_THAN_UINT32(100, longestIdleGap);
}
void test_already_available_bytes_have_no_added_sleep() {
  BambuMqttTransport transport;
  connack(transport, 0);
  transport.socketOpen = true;
  TEST_ASSERT_EQUAL_INT(4, transport.available());
  for (uint8_t expected : {0x20, 0x02, 0x00, 0x00}) {
    TEST_ASSERT_GREATER_THAN_INT(0, transport.available());
    TEST_ASSERT_EQUAL_INT(expected, transport.read());
  }
  TEST_ASSERT_EQUAL_UINT32(0, sleepCount);
}
void test_broker_auth_rejection_is_preserved() {
  for (uint8_t rc : {4,5}) {
    BambuMqttTransport transport;
    connack(transport, clockMs, rc);
    PubSubClient mqtt(transport);
    mqtt.setServer("test.invalid", 8883).setSocketTimeout(5);
    TEST_ASSERT_FALSE(mqtt.connect("test-client"));
    TEST_ASSERT_EQUAL_INT(rc, mqtt.state());
    TEST_ASSERT_FALSE(transport.socketOpen);
  }
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_missing_connack_returns_timeout_without_starving_idle);
  RUN_TEST(test_delayed_connack_connects_while_idle_keeps_running);
  RUN_TEST(test_fragmented_connack_readbyte_wait_also_allows_idle);
  RUN_TEST(test_fragmented_live_publish_preserves_payload_and_yields);
  RUN_TEST(test_already_available_bytes_have_no_added_sleep);
  RUN_TEST(test_broker_auth_rejection_is_preserved);
  return UNITY_END();
}
