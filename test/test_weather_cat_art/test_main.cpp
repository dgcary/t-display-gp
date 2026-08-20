#include <unity.h>

#include "WeatherCatArt.h"

void setUp() {}
void tearDown() {}

void test_selected_style_is_hand_painted_watercolor() {
  TEST_ASSERT_EQUAL(WeatherCatStyle::HAND_PAINTED_WATERCOLOR, WeatherCatArt::style());
  TEST_ASSERT_TRUE(WeatherCatArt::CANVAS_WIDTH <= 90);
  TEST_ASSERT_TRUE(WeatherCatArt::CANVAS_HEIGHT <= 90);
}

void test_weather_moods_map_to_distinct_watercolor_accessories() {
  TEST_ASSERT_EQUAL(CatAccessory::NONE, WeatherCatArt::pose(CatMood::HAPPY, 0).accessory);
  TEST_ASSERT_EQUAL(CatAccessory::SWEAT, WeatherCatArt::pose(CatMood::HOT, 0).accessory);
  TEST_ASSERT_EQUAL(CatAccessory::NONE, WeatherCatArt::pose(CatMood::CALM, 0).accessory);
  TEST_ASSERT_EQUAL(CatAccessory::SLEEP_MARK, WeatherCatArt::pose(CatMood::SLEEPY, 0).accessory);
  TEST_ASSERT_EQUAL(CatAccessory::UMBRELLA, WeatherCatArt::pose(CatMood::RAINY, 0).accessory);
  TEST_ASSERT_EQUAL(CatAccessory::SCARF, WeatherCatArt::pose(CatMood::COLD, 0).accessory);
  TEST_ASSERT_EQUAL(CatAccessory::LIGHTNING, WeatherCatArt::pose(CatMood::STARTLED, 0).accessory);
}

void test_pose_preserves_expression_contract() {
  TEST_ASSERT_TRUE(WeatherCatArt::pose(CatMood::HAPPY, 0).smile);
  TEST_ASSERT_TRUE(WeatherCatArt::pose(CatMood::HAPPY, 0).blush);
  TEST_ASSERT_TRUE(WeatherCatArt::pose(CatMood::SLEEPY, 0).eyesClosed);
  TEST_ASSERT_TRUE(WeatherCatArt::pose(CatMood::COLD, 0).blush);
  TEST_ASSERT_TRUE(WeatherCatArt::pose(CatMood::STARTLED, 0).eyesWide);
}

void test_two_frames_animate_gently_without_leaving_canvas() {
  const WeatherCatPose first = WeatherCatArt::pose(CatMood::CALM, 0);
  const WeatherCatPose second = WeatherCatArt::pose(CatMood::CALM, 1);
  TEST_ASSERT_NOT_EQUAL(first.bodyBob, second.bodyBob);
  TEST_ASSERT_NOT_EQUAL(first.tailOffset, second.tailOffset);
  TEST_ASSERT_TRUE(first.bodyBob >= 0 && first.bodyBob <= 2);
  TEST_ASSERT_TRUE(second.bodyBob >= 0 && second.bodyBob <= 2);
  TEST_ASSERT_TRUE(first.tailOffset >= -2 && first.tailOffset <= 2);
  TEST_ASSERT_TRUE(second.tailOffset >= -2 && second.tailOffset <= 2);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_selected_style_is_hand_painted_watercolor);
  RUN_TEST(test_weather_moods_map_to_distinct_watercolor_accessories);
  RUN_TEST(test_pose_preserves_expression_contract);
  RUN_TEST(test_two_frames_animate_gently_without_leaving_canvas);
  return UNITY_END();
}
