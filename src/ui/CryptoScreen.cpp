#include "CryptoScreen.h"

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include <cstdio>

#include "CryptoController.h"

namespace {
constexpr uint16_t BG = 0x0000;
constexpr uint16_t PANEL = 0x1082;
constexpr uint16_t BORDER = 0x39E7;
constexpr uint16_t TEXT = 0xFFFF;
constexpr uint16_t MUTED = 0x9CF3;
constexpr uint16_t UP = 0xF800;
constexpr uint16_t DOWN = 0x07E0;
const char* SYMBOLS[] = {"BTC", "ETH", "SOL"};

void formatPrice(double value, char* out, size_t size) {
  if (value >= 10000.0) std::snprintf(out, size, "$%.0f", value);
  else if (value >= 1000.0) std::snprintf(out, size, "$%.1f", value);
  else std::snprintf(out, size, "$%.2f", value);
}
}

void CryptoScreen::begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont) {
  display_ = &display;
  unicodeFont_ = &unicodeFont;
}

void CryptoScreen::render(const CryptoViewModel& view, bool) {
  if (!display_) return;
  display_->fillScreen(BG);
  display_->setTextSize(1);
  display_->setTextFont(2);
  display_->setTextDatum(TL_DATUM);
  display_->setTextColor(TEXT, BG);
  display_->drawString("CRYPTO", 10, 7);
  display_->setTextDatum(TR_DATUM);
  display_->setTextColor(MUTED, BG);
  display_->drawString("CoinGecko", 310, 7);
  display_->drawFastHLine(8, 25, 304, BORDER);

  if (!view.hasData) {
    if (unicodeFont_) {
      unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
      unicodeFont_->setBackgroundColor(BG);
      unicodeFont_->setForegroundColor(MUTED);
      unicodeFont_->setCursor(112, 91);
      unicodeFont_->print(view.wifiOnline ? "正在获取行情" : "网络未连接");
    }
  } else {
    for (int i = 0; i < 3; ++i) {
      const int y = 34 + i * 36;
      const auto& quote = view.snapshot.quotes[i];
      display_->fillRoundRect(8, y, 304, 30, 6, PANEL);
      display_->drawRoundRect(8, y, 304, 30, 6, BORDER);
      display_->setTextDatum(ML_DATUM);
      display_->setTextFont(2);
      display_->setTextColor(TEXT, PANEL);
      display_->drawString(SYMBOLS[i], 18, y + 15);
      char price[24] = {};
      formatPrice(quote.priceUsd, price, sizeof(price));
      display_->setTextDatum(MC_DATUM);
      display_->setTextFont(4);
      display_->setTextColor(TEXT, PANEL);
      display_->drawString(price, 170, y + 15);
      char change[20] = {};
      std::snprintf(change, sizeof(change), "%+.2f%%", quote.change24hPercent);
      display_->setTextDatum(MR_DATUM);
      display_->setTextFont(2);
      display_->setTextColor(quote.change24hPercent >= 0.0 ? UP : DOWN, PANEL);
      display_->drawString(change, 302, y + 15);
    }
  }

  display_->setTextDatum(BL_DATUM);
  display_->setTextFont(1);
  display_->setTextColor(MUTED, BG);
  const char* status = !view.wifiOnline ? "OFFLINE" : view.stale ? "STALE" :
                       view.error != CryptoError::NONE ? "ERROR / CACHE" :
                       view.requestInFlight ? "UPDATING" : "60s REFRESH";
  display_->drawString(status, 8, 166);
  display_->setTextDatum(BR_DATUM);
  display_->drawString("Data: CoinGecko", 312, 166);
}
