#include <unity.h>

#include "Formatters.h"

void setUp() {}
void tearDown() {}

void test_price_and_percent_formatting() {
  TEST_ASSERT_EQUAL_STRING("1410.25", formatPrice(1410.25).c_str());
  TEST_ASSERT_EQUAL_STRING("+1.23%", formatPercent(1.234).c_str());
  TEST_ASSERT_EQUAL_STRING("-0.40%", formatPercent(-0.4).c_str());
  TEST_ASSERT_EQUAL_STRING("0.00%", formatPercent(0.0).c_str());
}

void test_volume_and_amount_formatting() {
  TEST_ASSERT_EQUAL_STRING("123.4万手", formatVolume(1234000).c_str());
  TEST_ASSERT_EQUAL_STRING("12.35亿", formatAmount(1235000000.0).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_price_and_percent_formatting);
  RUN_TEST(test_volume_and_amount_formatting);
  return UNITY_END();
}
