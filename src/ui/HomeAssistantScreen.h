#pragma once

#include <string>

class TFT_eSPI;
class U8g2_for_TFT_eSPI;
struct HomeAssistantViewModel;

class HomeAssistantScreen {
 public:
  void begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont);
  void render(const HomeAssistantViewModel& view, const std::string& setupUrl, bool fullRedraw);
 private:
  TFT_eSPI* display_ = nullptr;
  U8g2_for_TFT_eSPI* unicodeFont_ = nullptr;
};
