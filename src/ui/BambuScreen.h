#pragma once

#include <string>

#include "BambuMqttService.h"
#include "BambuState.h"

class TFT_eSPI;
class U8g2_for_TFT_eSPI;

struct BambuViewModel {
  BambuState state;
  BambuMqttStatus service;
  std::string printerName;
};

class BambuScreen {
 public:
  void begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont);
  void render(const BambuViewModel& model, bool fullRedraw);

 private:
  TFT_eSPI* display_ = nullptr;
  U8g2_for_TFT_eSPI* unicodeFont_ = nullptr;
};
