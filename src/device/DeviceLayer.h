#pragma once

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include "ButtonInput.h"
#include "MarketClock.h"

class DeviceLayer {
 public:
  DeviceLayer();

  void begin();
  InputEvent pollButtons(uint32_t nowMs);
  bool wifiConnected() const;
  bool timeSynchronized() const;
  LocalDateTime localDateTime() const;

  TFT_eSPI& display() { return tft_; }
  U8g2_for_TFT_eSPI& unicodeFont() { return unicodeFont_; }

 private:
  void drawSmokeScreen();

  TFT_eSPI tft_;
  U8g2_for_TFT_eSPI unicodeFont_;
  ButtonInput previousButton_;
  ButtonInput nextButton_;
};
