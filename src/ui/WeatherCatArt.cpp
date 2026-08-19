#include "WeatherCatArt.h"

namespace WeatherCatArt {

WeatherCatStyle style() {
  return WeatherCatStyle::HAND_PAINTED_WATERCOLOR;
}

WeatherCatPose pose(CatMood mood, uint8_t animationFrame) {
  WeatherCatPose out;
  out.style = style();
  out.bodyBob = static_cast<int8_t>(animationFrame & 1U);
  out.tailOffset = (animationFrame & 1U) ? 2 : -1;

  switch (mood) {
    case CatMood::HAPPY:
      out.smile = true;
      out.blush = true;
      break;
    case CatMood::HOT:
      out.accessory = CatAccessory::SWEAT;
      out.blush = true;
      break;
    case CatMood::CALM:
      out.blush = true;
      break;
    case CatMood::SLEEPY:
      out.accessory = CatAccessory::SLEEP_MARK;
      out.eyesClosed = true;
      break;
    case CatMood::RAINY:
      out.accessory = CatAccessory::UMBRELLA;
      out.blush = true;
      break;
    case CatMood::COLD:
      out.accessory = CatAccessory::SCARF;
      out.blush = true;
      break;
    case CatMood::STARTLED:
      out.accessory = CatAccessory::LIGHTNING;
      out.eyesWide = true;
      break;
  }
  return out;
}

}  // namespace WeatherCatArt
