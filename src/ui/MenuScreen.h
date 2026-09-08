#pragma once

#include "AppShell.h"

class TFT_eSPI;
class U8g2_for_TFT_eSPI;

class MenuScreen final : public IMenuRenderer {
 public:
  void begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont);
  void render(const MenuViewModel& view, bool fullRedraw) override;

 private:
  TFT_eSPI* display_ = nullptr;
  U8g2_for_TFT_eSPI* unicodeFont_ = nullptr;
};
