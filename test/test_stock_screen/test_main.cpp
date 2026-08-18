#include <unity.h>

#include "StockScreen.h"

void setUp() {}
void tearDown() {}

void test_layout_matches_170x320_design() {
  TEST_ASSERT_EQUAL(0, StockScreenLayout::HEADER_Y0);
  TEST_ASSERT_EQUAL(28, StockScreenLayout::HEADER_Y1);
  TEST_ASSERT_EQUAL(30, StockScreenLayout::PRICE_Y0);
  TEST_ASSERT_EQUAL(86, StockScreenLayout::PRICE_Y1);
  TEST_ASSERT_EQUAL(8, StockScreenLayout::CHART_X0);
  TEST_ASSERT_EQUAL(162, StockScreenLayout::CHART_X1);
  TEST_ASSERT_EQUAL(172, StockScreenLayout::CHART_Y0);
  TEST_ASSERT_EQUAL(292, StockScreenLayout::CHART_Y1);
  TEST_ASSERT_EQUAL(296, StockScreenLayout::FOOTER_Y0);
  TEST_ASSERT_EQUAL(319, StockScreenLayout::FOOTER_Y1);
}

void test_chart_range_includes_prev_close_and_forces_minimum_span() {
  IntradaySeries flat = {{570, 100.0f, 100.0f, 0}, {571, 100.05f, 100.0f, 0}};
  const ChartRange padded = StockScreenMath::chartRange(flat, 100.0);
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, 99.9, padded.minPrice);
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, 100.1, padded.maxPrice);

  IntradaySeries wide = {{570, 98.0f, 98.0f, 0}, {900, 103.0f, 102.0f, 0}};
  const ChartRange natural = StockScreenMath::chartRange(wide, 100.0);
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, 98.0, natural.minPrice);
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, 103.0, natural.maxPrice);
}

void test_chart_x_preserves_small_lunch_gap() {
  TEST_ASSERT_EQUAL(StockScreenLayout::CHART_X0, StockScreenMath::chartX(570));
  const int morningClose = StockScreenMath::chartX(690);
  const int afternoonOpen = StockScreenMath::chartX(780);
  TEST_ASSERT_TRUE(afternoonOpen > morningClose);
  TEST_ASSERT_TRUE(afternoonOpen - morningClose >= 4);
  TEST_ASSERT_TRUE(afternoonOpen - morningClose <= 8);
  TEST_ASSERT_EQUAL(StockScreenLayout::CHART_X1, StockScreenMath::chartX(900));
}

void test_chart_y_maps_max_to_top_and_min_to_bottom() {
  const ChartRange range{90.0, 110.0};
  TEST_ASSERT_EQUAL(StockScreenLayout::CHART_Y0, StockScreenMath::chartY(110.0, range));
  TEST_ASSERT_EQUAL(StockScreenLayout::CHART_Y1, StockScreenMath::chartY(90.0, range));
  TEST_ASSERT_TRUE(StockScreenMath::chartY(100.0, range) > StockScreenLayout::CHART_Y0);
  TEST_ASSERT_TRUE(StockScreenMath::chartY(100.0, range) < StockScreenLayout::CHART_Y1);
}

void test_utf8_truncation_never_splits_chinese_codepoint() {
  TEST_ASSERT_EQUAL_STRING("贵州茅台", StockScreenMath::truncateUtf8("贵州茅台", 4).c_str());
  TEST_ASSERT_EQUAL_STRING("贵州茅…", StockScreenMath::truncateUtf8("贵州茅台股份", 4).c_str());
  TEST_ASSERT_EQUAL_STRING("A股…", StockScreenMath::truncateUtf8("A股票价格", 3).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_layout_matches_170x320_design);
  RUN_TEST(test_chart_range_includes_prev_close_and_forces_minimum_span);
  RUN_TEST(test_chart_x_preserves_small_lunch_gap);
  RUN_TEST(test_chart_y_maps_max_to_top_and_min_to_bottom);
  RUN_TEST(test_utf8_truncation_never_splits_chinese_codepoint);
  return UNITY_END();
}
