#include <unity.h>

#include "StockScreen.h"

void setUp() {}
void tearDown() {}

void test_layout_fits_320x170_landscape() {
  TEST_ASSERT_EQUAL(320, StockScreenLayout::SCREEN_WIDTH);
  TEST_ASSERT_EQUAL(170, StockScreenLayout::SCREEN_HEIGHT);
  TEST_ASSERT_TRUE(StockScreenLayout::LEFT_X0 >= 0);
  TEST_ASSERT_TRUE(StockScreenLayout::LEFT_X1 < StockScreenLayout::SCREEN_WIDTH);
  TEST_ASSERT_TRUE(StockScreenLayout::CHART_X0 > StockScreenLayout::LEFT_X1);
  TEST_ASSERT_TRUE(StockScreenLayout::CHART_X1 < StockScreenLayout::SCREEN_WIDTH);
  TEST_ASSERT_TRUE(StockScreenLayout::CHART_Y0 >= 0);
  TEST_ASSERT_TRUE(StockScreenLayout::CHART_Y1 < StockScreenLayout::SCREEN_HEIGHT);
  TEST_ASSERT_TRUE(StockScreenLayout::FOOTER_Y1 < StockScreenLayout::SCREEN_HEIGHT);
  TEST_ASSERT_TRUE(StockScreenLayout::CHART_Y1 - StockScreenLayout::CHART_Y0 + 1 >= 145);
}

void test_chart_range_includes_prev_close_open_and_forces_minimum_span() {
  IntradaySeries flat = {{570, 100.0f, 100.0f, 0}, {571, 100.05f, 100.0f, 0}};
  const ChartRange padded = StockScreenMath::chartRange(flat, 100.0, 0.0);
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, 99.9, padded.minPrice);
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, 100.1, padded.maxPrice);

  IntradaySeries wide = {{570, 99.0f, 99.0f, 0}, {900, 101.0f, 100.0f, 0}};
  const ChartRange withOpen = StockScreenMath::chartRange(wide, 100.0, 103.0);
  TEST_ASSERT_TRUE(withOpen.minPrice <= 99.0);
  TEST_ASSERT_TRUE(withOpen.maxPrice >= 103.0);

  const ChartRange invalidOpen = StockScreenMath::chartRange(wide, 100.0, 0.0);
  TEST_ASSERT_DOUBLE_WITHIN(0.0001, 101.0, invalidOpen.maxPrice);
}

void test_chart_x_preserves_small_lunch_gap_in_right_panel() {
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

void test_provider_summary_distinguishes_quote_and_intraday_sources() {
  TEST_ASSERT_EQUAL_STRING("Q:EM", StockScreenText::providerSummary(
                                      ProviderId::EAST_MONEY, ProviderId::EAST_MONEY, false).c_str());
  TEST_ASSERT_EQUAL_STRING("Q:TX I:TX", StockScreenText::providerSummary(
                                           ProviderId::TENCENT, ProviderId::TENCENT, true).c_str());
  TEST_ASSERT_EQUAL_STRING("Q:TX I:EM", StockScreenText::providerSummary(
                                           ProviderId::TENCENT, ProviderId::EAST_MONEY, true).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_layout_fits_320x170_landscape);
  RUN_TEST(test_chart_range_includes_prev_close_open_and_forces_minimum_span);
  RUN_TEST(test_chart_x_preserves_small_lunch_gap_in_right_panel);
  RUN_TEST(test_chart_y_maps_max_to_top_and_min_to_bottom);
  RUN_TEST(test_utf8_truncation_never_splits_chinese_codepoint);
  RUN_TEST(test_provider_summary_distinguishes_quote_and_intraday_sources);
  return UNITY_END();
}
