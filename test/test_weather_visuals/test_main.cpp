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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_weather_codes_map_to_visual_kinds);
  return UNITY_END();
}
