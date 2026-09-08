#pragma once

#include "DeviceInfoModel.h"

class TFT_eSPI;
class U8g2_for_TFT_eSPI;

class DeviceInfoScreen {
 public:
  void begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont);
  void render(const DeviceInfoViewModel& model, bool fullRedraw);

 private:
  TFT_eSPI* display_ = nullptr;
  U8g2_for_TFT_eSPI* unicodeFont_ = nullptr;
};
