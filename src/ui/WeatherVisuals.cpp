#include "WeatherVisuals.h"

namespace WeatherVisuals {

WeatherVisualKind kindForCode(int code) {
  if (code == 0) return WeatherVisualKind::CLEAR;
  if (code >= 1 && code <= 3) return WeatherVisualKind::CLOUDY;
  if (code == 45 || code == 48) return WeatherVisualKind::FOG;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    return WeatherVisualKind::RAIN;
  }
  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) {
    return WeatherVisualKind::SNOW;
  }
  if (code == 95 || code == 96 || code == 99) return WeatherVisualKind::STORM;
  return WeatherVisualKind::OTHER;
}

CatMood catMood(int weatherCode, float apparentTemperature) {
  const WeatherVisualKind kind = kindForCode(weatherCode);
  if (kind == WeatherVisualKind::STORM) return CatMood::STARTLED;
  if (kind == WeatherVisualKind::SNOW || apparentTemperature <= 5.0f) return CatMood::COLD;
  if (kind == WeatherVisualKind::RAIN) return CatMood::RAINY;
  if (kind == WeatherVisualKind::FOG) return CatMood::SLEEPY;
  if (apparentTemperature >= 35.0f) return CatMood::HOT;
  if (kind == WeatherVisualKind::CLEAR) return CatMood::HAPPY;
  return CatMood::CALM;
}

uint8_t animationFrame(uint32_t nowMs) {
  return static_cast<uint8_t>((nowMs / 500U) & 1U);
}

}  // namespace WeatherVisuals
