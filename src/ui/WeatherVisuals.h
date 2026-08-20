#pragma once

#include <cstdint>

enum class WeatherVisualKind {
  CLEAR,
  CLOUDY,
  FOG,
  RAIN,
  SNOW,
  STORM,
  OTHER,
};

enum class CatMood {
  HAPPY,
  HOT,
  CALM,
  SLEEPY,
  RAINY,
  COLD,
  STARTLED,
};

namespace WeatherVisuals {
WeatherVisualKind kindForCode(int weatherCode);
CatMood catMood(int weatherCode, float apparentTemperature);
uint8_t animationFrame(uint32_t nowMs);
}  // namespace WeatherVisuals
