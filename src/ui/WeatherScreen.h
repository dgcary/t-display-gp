#pragma once

#include <array>
#include <cstdint>

#include "BadApplePlayback.h"
#include "WeatherController.h"

class TFT_eSPI;
class U8g2_for_TFT_eSPI;

class WeatherScreen {
 public:
  void begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont);
  void resetBadApple();
  void render(const WeatherViewModel& model, bool fullRedraw, uint32_t animationFrame);
  void renderAnimation(const WeatherViewModel& model, uint32_t animationFrame);

 private:
  bool decodeBadApple(uint32_t targetFrame);
  void drawBadApple(uint32_t targetFrame);

  TFT_eSPI* display_ = nullptr;
  U8g2_for_TFT_eSPI* unicodeFont_ = nullptr;
  std::array<uint8_t, BadApplePlayback::FRAME_BYTES> badAppleFrame_{};
  uint32_t decodedBadAppleFrame_ = 0;
  bool badAppleFrameValid_ = false;
};
