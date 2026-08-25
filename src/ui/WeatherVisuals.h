#pragma once

enum class WeatherVisualKind {
  CLEAR,
  CLOUDY,
  FOG,
  RAIN,
  SNOW,
  STORM,
  OTHER,
};

namespace WeatherVisuals {
WeatherVisualKind kindForCode(int weatherCode);
}  // namespace WeatherVisuals
