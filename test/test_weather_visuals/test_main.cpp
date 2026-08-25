#include <unity.h>

#include "WeatherVisuals.h"

void setUp() {}
void tearDown() {}

void test_weather_codes_map_to_visual_kinds() {
  TEST_ASSERT_EQUAL(WeatherVisualKind::CLEAR, WeatherVisuals::kindForCode(0));
  TEST_ASSERT_EQUAL(WeatherVisualKind::CLOUDY, WeatherVisuals::kindForCode(2));
  TEST_ASSERT_EQUAL(WeatherVisualKind::FOG, WeatherVisuals::kindForCode(45));
  TEST_ASSERT_EQUAL(WeatherVisualKind::RAIN, WeatherVisuals::kindForCode(61));
  TEST_ASSERT_EQUAL(WeatherVisualKind::RAIN, WeatherVisuals::kindForCode(82));
  TEST_ASSERT_EQUAL(WeatherVisualKind::SNOW, WeatherVisuals::kindForCode(73));
  TEST_ASSERT_EQUAL(WeatherVisualKind::STORM, WeatherVisuals::kindForCode(95));
  TEST_ASSERT_EQUAL(WeatherVisualKind::OTHER, WeatherVisuals::kindForCode(90));
}

void test_cat_mood_reacts_to_weather_and_temperature() {
  TEST_ASSERT_EQUAL(CatMood::HAPPY, WeatherVisuals::catMood(0, 27.0f));
  TEST_ASSERT_EQUAL(CatMood::HOT, WeatherVisuals::catMood(0, 36.0f));
  TEST_ASSERT_EQUAL(CatMood::CALM, WeatherVisuals::catMood(2, 28.0f));
  TEST_ASSERT_EQUAL(CatMood::SLEEPY, WeatherVisuals::catMood(45, 20.0f));
  TEST_ASSERT_EQUAL(CatMood::RAINY, WeatherVisuals::catMood(63, 22.0f));
  TEST_ASSERT_EQUAL(CatMood::COLD, WeatherVisuals::catMood(73, -2.0f));
  TEST_ASSERT_EQUAL(CatMood::STARTLED, WeatherVisuals::catMood(96, 25.0f));
  TEST_ASSERT_EQUAL(CatMood::COLD, WeatherVisuals::catMood(2, 3.0f));
}

void test_animation_frame_advances_at_ten_fps() {
  TEST_ASSERT_EQUAL_UINT8(0, WeatherVisuals::animationFrame(0));
  TEST_ASSERT_EQUAL_UINT8(0, WeatherVisuals::animationFrame(99));
  TEST_ASSERT_EQUAL_UINT8(1, WeatherVisuals::animationFrame(100));
  TEST_ASSERT_EQUAL_UINT8(2, WeatherVisuals::animationFrame(200));
  TEST_ASSERT_EQUAL_UINT8(9, WeatherVisuals::animationFrame(999));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_weather_codes_map_to_visual_kinds);
  RUN_TEST(test_cat_mood_reacts_to_weather_and_temperature);
  RUN_TEST(test_animation_frame_advances_at_ten_fps);
  return UNITY_END();
}
