#pragma once

#include "NixieClockModel.h"

class TFT_eSPI;
class U8g2_for_TFT_eSPI;

class NixieClockScreen {
 public:
  void begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont);
  void render(const NixieClockViewModel& model, bool fullRedraw);

 private:
  void drawStaticFrame();
  void drawDigitSlot(int slot, int digit, bool valid);
  void drawColon(bool visible);
  void drawFooter(const NixieClockViewModel& model);

  TFT_eSPI* display_ = nullptr;
  U8g2_for_TFT_eSPI* unicodeFont_ = nullptr;
  NixieClockViewModel previous_;
  bool hasPrevious_ = false;
};
