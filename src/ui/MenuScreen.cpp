#include "MenuScreen.h"

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include <cstdio>

#include "UiTheme.h"

void MenuScreen::begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont) {
  display_ = &display;
  unicodeFont_ = &unicodeFont;
}

void MenuScreen::render(const MenuViewModel& view, bool) {
  if (!display_ || !unicodeFont_) return;

  display_->fillScreen(UiTheme::BACKGROUND);
  display_->drawFastHLine(0, 25, 320, UiTheme::GRID);

  unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
  unicodeFont_->setForegroundColor(UiTheme::TEXT);
  unicodeFont_->setBackgroundColor(UiTheme::BACKGROUND);
  unicodeFont_->setCursor(10, 18);
  unicodeFont_->print("T-Display GP / 主菜单");

  if (view.items.empty() || view.selectedIndex >= view.items.size()) {
    unicodeFont_->setCursor(105, 88);
    unicodeFont_->print("暂无应用");
    return;
  }

  const size_t count = view.items.size();
  const size_t selected = view.selectedIndex;
  const size_t previous = selected == 0 ? count - 1 : selected - 1;
  const size_t next = (selected + 1) % count;

  display_->drawRoundRect(84, 39, 152, 82, 10, UiTheme::CHART);
  display_->drawRoundRect(87, 42, 146, 76, 8, UiTheme::GRID);

  unicodeFont_->setForegroundColor(UiTheme::TEXT);
  unicodeFont_->setCursor(143, 76);
  unicodeFont_->print(view.items[selected].name);

  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  unicodeFont_->setCursor(8, 79);
  unicodeFont_->print(view.items[previous].name);
  unicodeFont_->setCursor(266, 79);
  unicodeFont_->print(view.items[next].name);

  char indexText[24] = {};
  std::snprintf(indexText, sizeof(indexText), "%u / %u",
                static_cast<unsigned>(selected + 1), static_cast<unsigned>(count));
  unicodeFont_->setCursor(143, 105);
  unicodeFont_->print(indexText);

  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  unicodeFont_->setCursor(8, 157);
  unicodeFont_->print("短按左右选择   长按右键进入");
}
