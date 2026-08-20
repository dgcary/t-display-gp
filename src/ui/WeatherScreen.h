#pragma once

#include <cstdint>

#include "WeatherController.h"

class TFT_eSPI;
class U8g2_for_TFT_eSPI;

class WeatherScreen {
 public:
  void begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont);
  void render(const WeatherViewModel& model, bool fullRedraw, uint8_t animationFrame);
  void renderAnimation(const WeatherViewModel& model, uint8_t animationFrame);

 private:
  void drawPetScene(const WeatherViewModel& model, uint8_t animationFrame);

  TFT_eSPI* display_ = nullptr;
  U8g2_for_TFT_eSPI* unicodeFont_ = nullptr;
};
