#pragma once

#include <cstdint>

#include "WeatherVisuals.h"

enum class WeatherCatStyle : uint8_t {
  HAND_PAINTED_WATERCOLOR,
};

enum class CatAccessory : uint8_t {
  NONE,
  SWEAT,
  SLEEP_MARK,
  UMBRELLA,
  SCARF,
  LIGHTNING,
};

struct WeatherCatPose {
  WeatherCatStyle style = WeatherCatStyle::HAND_PAINTED_WATERCOLOR;
  CatAccessory accessory = CatAccessory::NONE;
  int8_t bodyBob = 0;
  int8_t tailOffset = 0;
  bool eyesClosed = false;
  bool eyesWide = false;
  bool smile = false;
  bool blush = false;
};

namespace WeatherCatArt {
constexpr int CANVAS_WIDTH = 84;
constexpr int CANVAS_HEIGHT = 84;

WeatherCatStyle style();
WeatherCatPose pose(CatMood mood, uint8_t animationFrame);
}  // namespace WeatherCatArt
