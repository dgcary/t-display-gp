#include "NixieClockScreen.h"

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include <cstdio>

namespace {
constexpr int SCREEN_WIDTH = 320;
constexpr int SCREEN_HEIGHT = 170;
constexpr int TUBE_Y = 30;
constexpr int TUBE_W = 52;
constexpr int TUBE_H = 92;
constexpr int TUBE_X[4] = {18, 79, 189, 250};
constexpr int COLON_X = 160;

constexpr uint16_t BG = 0x0000;
constexpr uint16_t GLASS = 0x1882;
constexpr uint16_t GLASS_EDGE = 0x39A4;
constexpr uint16_t GLASS_GLOW = 0x7A40;
constexpr uint16_t TUBE_INNER = 0x0800;
constexpr uint16_t DIGIT_GLOW = 0xA200;
constexpr uint16_t DIGIT_WARM = 0xFD20;
constexpr uint16_t DIGIT_CORE = 0xFFE0;
constexpr uint16_t TEXT_DIM = 0x9A60;
constexpr uint16_t TEXT_MUTED = 0x7BEF;

int digitForSlot(const NixieClockViewModel& model, int slot) {
  if (!model.timeValid) return -1;
  switch (slot) {
    case 0: return model.hour / 10;
    case 1: return model.hour % 10;
    case 2: return model.minute / 10;
    case 3: return model.minute % 10;
    default: return -1;
  }
}
}  // namespace

void NixieClockScreen::begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont) {
  display_ = &display;
  unicodeFont_ = &unicodeFont;
}

void NixieClockScreen::drawStaticFrame() {
  if (!display_) return;
  display_->fillScreen(BG);
  display_->setTextDatum(TL_DATUM);
  display_->setTextFont(2);
  display_->setTextSize(1);
  display_->setTextColor(TEXT_DIM, BG);
  display_->drawString("NIXIE CLOCK", 12, 7);
  display_->setTextDatum(TR_DATUM);
  display_->setTextColor(TEXT_MUTED, BG);
  display_->drawString("LOCAL TIME", SCREEN_WIDTH - 12, 7);
  display_->drawFastHLine(8, 24, SCREEN_WIDTH - 16, GLASS);
}

void NixieClockScreen::drawDigitSlot(int slot, int digit, bool valid) {
  if (!display_ || slot < 0 || slot >= 4) return;
  const int x = TUBE_X[slot];

  display_->fillRoundRect(x, TUBE_Y, TUBE_W, TUBE_H, 11, GLASS);
  display_->drawRoundRect(x, TUBE_Y, TUBE_W, TUBE_H, 11, GLASS_GLOW);
  display_->drawRoundRect(x + 2, TUBE_Y + 2, TUBE_W - 4, TUBE_H - 4, 9, GLASS_EDGE);
  display_->fillRoundRect(x + 5, TUBE_Y + 5, TUBE_W - 10, TUBE_H - 10, 7, TUBE_INNER);
  display_->drawFastHLine(x + 9, TUBE_Y + TUBE_H - 12, TUBE_W - 18, GLASS_EDGE);

  char text[4] = "-";
  if (valid && digit >= 0 && digit <= 9) {
    std::snprintf(text, sizeof(text), "%d", digit);
  }

  const int cx = x + TUBE_W / 2;
  const int cy = TUBE_Y + 45;
  display_->setTextDatum(MC_DATUM);
  display_->setTextFont(4);
  display_->setTextSize(2);

  display_->setTextColor(DIGIT_GLOW);
  display_->drawString(text, cx - 1, cy);
  display_->drawString(text, cx + 1, cy);
  display_->drawString(text, cx, cy - 1);
  display_->drawString(text, cx, cy + 1);

  display_->setTextColor(valid ? DIGIT_WARM : GLASS_EDGE);
  display_->drawString(text, cx, cy);

  if (valid) {
    display_->setTextSize(1);
    display_->setTextFont(2);
    display_->setTextColor(DIGIT_CORE);
    display_->drawString(text, cx, cy + 1);
  }

  display_->setTextSize(1);
}

void NixieClockScreen::drawColon(bool visible) {
  if (!display_) return;
  display_->fillRect(COLON_X - 11, 55, 22, 44, BG);
  const uint16_t glow = visible ? DIGIT_GLOW : GLASS;
  const uint16_t core = visible ? DIGIT_WARM : GLASS_EDGE;
  display_->fillCircle(COLON_X, 67, 5, glow);
  display_->fillCircle(COLON_X, 87, 5, glow);
  display_->fillCircle(COLON_X, 67, 2, core);
  display_->fillCircle(COLON_X, 87, 2, core);
}

void NixieClockScreen::drawFooter(const NixieClockViewModel& model) {
  if (!display_ || !unicodeFont_) return;
  display_->fillRect(0, 128, SCREEN_WIDTH, SCREEN_HEIGHT - 128, BG);

  if (!model.timeValid) {
    unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
    unicodeFont_->setBackgroundColor(BG);
    unicodeFont_->setForegroundColor(TEXT_DIM);
    unicodeFont_->setCursor(103, 153);
    unicodeFont_->print("等待时间同步");
    return;
  }

  char date[32] = {};
  char seconds[16] = {};
  std::snprintf(date, sizeof(date), "%04d-%02d-%02d  %s", model.year, model.month, model.day,
                NixieClockModel::weekdayShort(model.dayOfWeek));
  std::snprintf(seconds, sizeof(seconds), "SEC %02d", model.second);

  display_->setTextFont(2);
  display_->setTextSize(1);
  display_->setTextDatum(TL_DATUM);
  display_->setTextColor(TEXT_DIM, BG);
  display_->drawString(date, 18, 141);
  display_->setTextDatum(TR_DATUM);
  display_->setTextColor(DIGIT_WARM, BG);
  display_->drawString(seconds, SCREEN_WIDTH - 18, 141);
}

void NixieClockScreen::render(const NixieClockViewModel& model, bool fullRedraw) {
  if (!display_) return;

  if (fullRedraw || !hasPrevious_) {
    drawStaticFrame();
    for (int slot = 0; slot < 4; ++slot) {
      drawDigitSlot(slot, digitForSlot(model, slot), model.timeValid);
    }
    drawColon(model.timeValid && model.colonVisible);
    drawFooter(model);
    previous_ = model;
    hasPrevious_ = true;
    return;
  }

  const bool validityChanged = previous_.timeValid != model.timeValid;
  if (validityChanged) {
    for (int slot = 0; slot < 4; ++slot) {
      drawDigitSlot(slot, digitForSlot(model, slot), model.timeValid);
    }
  } else {
    for (int slot = 0; slot < 4; ++slot) {
      const int previousDigit = digitForSlot(previous_, slot);
      const int nextDigit = digitForSlot(model, slot);
      if (previousDigit != nextDigit) drawDigitSlot(slot, nextDigit, model.timeValid);
    }
  }

  if (validityChanged || previous_.colonVisible != model.colonVisible) {
    drawColon(model.timeValid && model.colonVisible);
  }

  if (validityChanged || previous_.second != model.second || previous_.day != model.day ||
      previous_.month != model.month || previous_.year != model.year ||
      previous_.dayOfWeek != model.dayOfWeek) {
    drawFooter(model);
  }

  previous_ = model;
  hasPrevious_ = true;
}
