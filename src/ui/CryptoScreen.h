#pragma once

class TFT_eSPI;
class U8g2_for_TFT_eSPI;
struct CryptoViewModel;

class CryptoScreen {
 public:
  void begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont);
  void render(const CryptoViewModel& view, bool fullRedraw);
 private:
  TFT_eSPI* display_ = nullptr;
  U8g2_for_TFT_eSPI* unicodeFont_ = nullptr;
};
