#include "HomeAssistantScreen.h"

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include "HomeAssistantController.h"

namespace {
constexpr uint16_t BG = 0x0000;
constexpr uint16_t PANEL = 0x1082;
constexpr uint16_t BORDER = 0x39E7;
constexpr uint16_t TEXT = 0xFFFF;
constexpr uint16_t MUTED = 0x9CF3;
constexpr uint16_t OK = 0x07E0;
constexpr uint16_t BAD = 0xF800;
}

void HomeAssistantScreen::begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont) {
  display_ = &display;
  unicodeFont_ = &unicodeFont;
}

void HomeAssistantScreen::render(const HomeAssistantViewModel& view, const std::string& setupUrl, bool) {
  if (!display_ || !unicodeFont_) return;
  display_->fillScreen(BG);
  display_->setTextFont(2);
  display_->setTextSize(1);
  display_->setTextDatum(TL_DATUM);
  display_->setTextColor(TEXT, BG);
  display_->drawString("HOME ASSISTANT", 10, 7);
  display_->setTextDatum(TR_DATUM);
  display_->setTextColor(view.configured ? OK : MUTED, BG);
  display_->drawString(view.configured ? "READ ONLY" : "SETUP", 310, 7);
  display_->drawFastHLine(8, 25, 304, BORDER);

  if (!view.configured) {
    unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
    unicodeFont_->setBackgroundColor(BG);
    unicodeFont_->setForegroundColor(MUTED);
    unicodeFont_->setCursor(120, 78);
    unicodeFont_->print("未配置");
    display_->setTextDatum(MC_DATUM);
    display_->setTextFont(2);
    display_->setTextColor(TEXT, BG);
    display_->drawString(setupUrl.c_str(), 160, 105);
  } else {
    for (size_t i = 0; i < view.entityCount && i < 4; ++i) {
      const int y = 31 + static_cast<int>(i) * 29;
      const auto& entity = view.entities[i];
      display_->fillRoundRect(7, y, 306, 25, 5, PANEL);
      display_->drawRoundRect(7, y, 306, 25, 5, BORDER);
      unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
      unicodeFont_->setBackgroundColor(PANEL);
      unicodeFont_->setForegroundColor(TEXT);
      unicodeFont_->setCursor(14, y + 17);
      unicodeFont_->print(entity.label.c_str());
      display_->setTextFont(2);
      display_->setTextDatum(MR_DATUM);
      if (entity.hasData) {
        display_->setTextColor(TEXT, PANEL);
        std::string value = entity.state;
        if (!entity.unit.empty()) value += " " + entity.unit;
        if (value.size() > 26) value.resize(26);
        display_->drawString(value.c_str(), 303, y + 12);
      } else {
        display_->setTextColor(entity.error == HomeAssistantError::NONE ? MUTED : BAD, PANEL);
        display_->drawString(entity.error == HomeAssistantError::NONE ? "--" : "ERR", 303, y + 12);
      }
    }
  }

  display_->setTextDatum(BL_DATUM);
  display_->setTextFont(1);
  display_->setTextColor(MUTED, BG);
  const char* status = !view.wifiOnline ? "OFFLINE" : view.stale ? "STALE" :
                       view.requestInFlight ? "UPDATING" : view.configured ? "READY" : "PORT 8081";
  display_->drawString(status, 8, 166);
  display_->setTextDatum(BR_DATUM);
  display_->drawString("GPIO0 long: menu", 312, 166);
}
