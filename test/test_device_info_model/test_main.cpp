#include <unity.h>

#include "AppShell.h"
#include "DeviceInfoModel.h"

void setUp() {}
void tearDown() {}

void test_device_info_has_dedicated_app_id() {
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(AppId::MENU), static_cast<int>(AppId::DEVICE_INFO));
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(AppId::STOCK), static_cast<int>(AppId::DEVICE_INFO));
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(AppId::WEATHER), static_cast<int>(AppId::DEVICE_INFO));
}

void test_uptime_formatter_handles_days_and_zero() {
  TEST_ASSERT_EQUAL_STRING("00:00:00", DeviceInfoFormatting::uptime(0).c_str());
  TEST_ASSERT_EQUAL_STRING("01:01:01", DeviceInfoFormatting::uptime(3661000).c_str());
  TEST_ASSERT_EQUAL_STRING("1d 01:01:01", DeviceInfoFormatting::uptime(90061000).c_str());
}

void test_heap_formatter_uses_kilobytes() {
  TEST_ASSERT_EQUAL_STRING("0 KB", DeviceInfoFormatting::kilobytes(0).c_str());
  TEST_ASSERT_EQUAL_STRING("52 KB", DeviceInfoFormatting::kilobytes(53248).c_str());
  TEST_ASSERT_EQUAL_STRING("1024 KB", DeviceInfoFormatting::kilobytes(1048576).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_device_info_has_dedicated_app_id);
  RUN_TEST(test_uptime_formatter_handles_days_and_zero);
  RUN_TEST(test_heap_formatter_uses_kilobytes);
  return UNITY_END();
}
