#include "StockScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "Formatters.h"
#include "UiTheme.h"

namespace {
constexpr int MORNING_OPEN = 9 * 60 + 30;
constexpr int MORNING_CLOSE = 11 * 60 + 30;
constexpr int AFTERNOON_OPEN = 13 * 60;
constexpr int AFTERNOON_CLOSE = 15 * 60;
constexpr int SESSION_PIXELS = 74;
constexpr int AFTERNOON_X0 = StockScreenLayout::CHART_X0 + SESSION_PIXELS + 6;

size_t utf8CharBytes(unsigned char lead) {
  if ((lead & 0x80U) == 0) return 1;
  if ((lead & 0xE0U) == 0xC0U) return 2;
  if ((lead & 0xF0U) == 0xE0U) return 3;
  if ((lead & 0xF8U) == 0xF0U) return 4;
  return 1;
}

bool hasContinuationBytes(const std::string& text, size_t pos, size_t bytes) {
  if (pos + bytes > text.size()) return false;
  for (size_t i = 1; i < bytes; ++i) {
    if ((static_cast<unsigned char>(text[pos + i]) & 0xC0U) != 0x80U) return false;
  }
  return true;
}

size_t codepointCount(const std::string& text) {
  size_t count = 0;
  for (size_t pos = 0; pos < text.size();) {
    size_t bytes = utf8CharBytes(static_cast<unsigned char>(text[pos]));
    if (!hasContinuationBytes(text, pos, bytes)) bytes = 1;
    pos += bytes;
    ++count;
  }
  return count;
}
}  // namespace

ChartRange StockScreenMath::chartRange(const IntradaySeries& series, double prevClose) {
  double minPrice = std::numeric_limits<double>::infinity();
  double maxPrice = -std::numeric_limits<double>::infinity();
  if (std::isfinite(prevClose) && prevClose > 0) {
    minPrice = prevClose;
    maxPrice = prevClose;
  }
  for (const auto& point : series) {
    if (!std::isfinite(point.price) || point.price <= 0) continue;
    minPrice = std::min(minPrice, static_cast<double>(point.price));
    maxPrice = std::max(maxPrice, static_cast<double>(point.price));
  }
  if (!std::isfinite(minPrice) || !std::isfinite(maxPrice)) return {0, 1};

  if (prevClose > 0 && std::isfinite(prevClose)) {
    const double halfMinimum = prevClose * 0.001;
    if (maxPrice - minPrice < prevClose * 0.002) {
      minPrice = std::min(minPrice, prevClose - halfMinimum);
      maxPrice = std::max(maxPrice, prevClose + halfMinimum);
    }
  }
  if (maxPrice <= minPrice) {
    const double padding = minPrice == 0 ? 1.0 : std::fabs(minPrice) * 0.001;
    minPrice -= padding;
    maxPrice += padding;
  }
  return {minPrice, maxPrice};
}

int StockScreenMath::chartX(uint16_t minuteOfDay) {
  const int minute = static_cast<int>(minuteOfDay);
  if (minute <= MORNING_OPEN) return StockScreenLayout::CHART_X0;
  if (minute <= MORNING_CLOSE) {
    const double fraction = static_cast<double>(minute - MORNING_OPEN) / (MORNING_CLOSE - MORNING_OPEN);
    return StockScreenLayout::CHART_X0 + static_cast<int>(std::lround(fraction * SESSION_PIXELS));
  }
  if (minute < AFTERNOON_OPEN) return AFTERNOON_X0 - 3;
  if (minute >= AFTERNOON_CLOSE) return StockScreenLayout::CHART_X1;
  const double fraction = static_cast<double>(minute - AFTERNOON_OPEN) / (AFTERNOON_CLOSE - AFTERNOON_OPEN);
  return AFTERNOON_X0 + static_cast<int>(std::lround(fraction * SESSION_PIXELS));
}

int StockScreenMath::chartY(double price, const ChartRange& range) {
  if (!(range.maxPrice > range.minPrice) || !std::isfinite(price)) return StockScreenLayout::CHART_Y1;
  const double clamped = std::max(range.minPrice, std::min(price, range.maxPrice));
  const double fraction = (clamped - range.minPrice) / (range.maxPrice - range.minPrice);
  const int height = StockScreenLayout::CHART_Y1 - StockScreenLayout::CHART_Y0;
  return StockScreenLayout::CHART_Y1 - static_cast<int>(std::lround(fraction * height));
}

std::string StockScreenMath::truncateUtf8(const std::string& text, size_t maxCodepoints) {
  if (maxCodepoints == 0) return {};
  if (codepointCount(text) <= maxCodepoints) return text;
  if (maxCodepoints == 1) return "…";

  const size_t keep = maxCodepoints - 1;
  size_t pos = 0;
  size_t count = 0;
  while (pos < text.size() && count < keep) {
    size_t bytes = utf8CharBytes(static_cast<unsigned char>(text[pos]));
    if (!hasContinuationBytes(text, pos, bytes)) bytes = 1;
    pos += bytes;
    ++count;
  }
  return text.substr(0, pos) + "…";
}

#ifdef ARDUINO
#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

namespace {
uint16_t priceColor(const StockViewModel& model) {
  if (!model.hasQuote || !model.quote) return UiTheme::NEUTRAL;
  if (model.quote->change > 0) return UiTheme::POSITIVE;
  if (model.quote->change < 0) return UiTheme::NEGATIVE;
  return UiTheme::NEUTRAL;
}

const char* marketText(MarketStatus status) {
  switch (status) {
    case MarketStatus::PRE_OPEN: return "待开盘";
    case MarketStatus::TRADING_AM:
    case MarketStatus::TRADING_PM: return "交易中";
    case MarketStatus::LUNCH_BREAK: return "午休";
    case MarketStatus::CLOSED: return "已收盘";
    case MarketStatus::NON_TRADING_DAY: return "休市";
    case MarketStatus::UNKNOWN: return "时间同步";
  }
  return "";
}

void drawAscii(TFT_eSPI& tft, const std::string& text, int x, int y, uint8_t font, uint16_t color) {
  tft.setTextColor(color, UiTheme::BACKGROUND);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(text.c_str(), x, y, font);
}
}  // namespace

void StockScreen::begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont) {
  display_ = &display;
  unicodeFont_ = &unicodeFont;
  unicodeFont_->setFontMode(1);
  unicodeFont_->setFontDirection(0);
  unicodeFont_->setBackgroundColor(UiTheme::BACKGROUND);
  rendered_ = false;
}

bool StockScreen::sameQuote(const RenderSignature& a, const RenderSignature& b) {
  return a.hasQuote == b.hasQuote && a.last == b.last && a.change == b.change &&
         a.changePercent == b.changePercent && a.open == b.open && a.high == b.high &&
         a.low == b.low && a.prevClose == b.prevClose && a.volume == b.volume && a.amount == b.amount;
}

bool StockScreen::sameIntraday(const RenderSignature& a, const RenderSignature& b) {
  return a.hasIntraday == b.hasIntraday && a.intradaySize == b.intradaySize &&
         a.intradayLastMinute == b.intradayLastMinute && a.intradayLastPrice == b.intradayLastPrice &&
         a.prevClose == b.prevClose;
}

StockScreen::RenderSignature StockScreen::signatureFor(const StockViewModel& model) const {
  RenderSignature result;
  result.symbol = model.symbol.canonical();
  result.displayName = model.displayName;
  result.index = model.index;
  result.count = model.count;
  result.marketStatus = model.marketStatus;
  result.wifiOnline = model.wifiOnline;
  result.provider = model.provider;
  result.errorBadge = model.errorBadge;
  result.hasQuote = model.hasQuote && model.quote;
  if (result.hasQuote) {
    result.last = model.quote->last;
    result.change = model.quote->change;
    result.changePercent = model.quote->changePercent;
    result.open = model.quote->open;
    result.high = model.quote->high;
    result.low = model.quote->low;
    result.prevClose = model.quote->prevClose;
    result.volume = model.quote->volume;
    result.amount = model.quote->amount;
  }
  result.hasIntraday = model.hasIntraday && model.intraday;
  if (result.hasIntraday) {
    result.intradaySize = model.intraday->size();
    if (!model.intraday->empty()) {
      result.intradayLastMinute = model.intraday->back().minuteOfDay;
      result.intradayLastPrice = model.intraday->back().price;
    }
  }
  return result;
}

void StockScreen::render(const StockViewModel& model, bool fullRedraw) {
  if (!display_ || !unicodeFont_) return;
  const RenderSignature next = signatureFor(model);
  const bool symbolChanged = previous_.symbol != next.symbol;
  const bool full = fullRedraw || !rendered_ || symbolChanged;
  if (full) {
    display_->fillScreen(UiTheme::BACKGROUND);
    drawHeader(model);
    drawQuote(model);
    drawMetrics(model);
    drawTurnover(model);
    drawChart(model);
    drawFooter(model);
  } else {
    if (previous_.displayName != next.displayName || previous_.marketStatus != next.marketStatus) drawHeader(model);
    if (!sameQuote(previous_, next)) {
      drawQuote(model);
      drawMetrics(model);
      drawTurnover(model);
    }
    if (!sameIntraday(previous_, next)) drawChart(model);
    if (previous_.index != next.index || previous_.count != next.count || previous_.wifiOnline != next.wifiOnline ||
        previous_.provider != next.provider || previous_.errorBadge != next.errorBadge ||
        previous_.marketStatus != next.marketStatus || !sameQuote(previous_, next)) {
      drawFooter(model);
    }
  }
  previous_ = next;
  rendered_ = true;
}

void StockScreen::drawHeader(const StockViewModel& model) {
  display_->fillRect(0, StockScreenLayout::HEADER_Y0, 170, StockScreenLayout::HEADER_Y1 + 1, UiTheme::BACKGROUND);
  unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
  unicodeFont_->setForegroundColor(UiTheme::TEXT);
  const std::string name = StockScreenMath::truncateUtf8(model.displayName, 6);
  unicodeFont_->drawUTF8(6, 16, name.c_str());
  drawAscii(*display_, model.symbol.code(), 111, 4, 1, UiTheme::MUTED);
  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  unicodeFont_->drawUTF8(6, 27, marketText(model.marketStatus));
}

void StockScreen::drawQuote(const StockViewModel& model) {
  const int height = StockScreenLayout::PRICE_Y1 - StockScreenLayout::PRICE_Y0 + 1;
  display_->fillRect(0, StockScreenLayout::PRICE_Y0, 170, height, UiTheme::BACKGROUND);
  if (!model.hasQuote || !model.quote) {
    drawAscii(*display_, "--", 8, 38, 4, UiTheme::NEUTRAL);
    return;
  }
  const uint16_t color = priceColor(model);
  drawAscii(*display_, formatPrice(model.quote->last), 6, 34, 4, color);
  std::string change = formatPrice(model.quote->change);
  if (model.quote->change > 0) change = "+" + change;
  drawAscii(*display_, change + "  " + formatPercent(model.quote->changePercent), 8, 70, 1, color);
}

void StockScreen::drawMetrics(const StockViewModel& model) {
  const int height = StockScreenLayout::METRICS_Y1 - StockScreenLayout::METRICS_Y0 + 1;
  display_->fillRect(0, StockScreenLayout::METRICS_Y0, 170, height, UiTheme::BACKGROUND);
  unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  const char* labels[4] = {"今开", "最高", "最低", "昨收"};
  const double values[4] = {
      model.hasQuote && model.quote ? model.quote->open : 0,
      model.hasQuote && model.quote ? model.quote->high : 0,
      model.hasQuote && model.quote ? model.quote->low : 0,
      model.hasQuote && model.quote ? model.quote->prevClose : 0};
  for (int i = 0; i < 4; ++i) {
    const int col = i % 2;
    const int row = i / 2;
    const int x = col == 0 ? 6 : 88;
    const int y = 101 + row * 27;
    unicodeFont_->drawUTF8(x, y, labels[i]);
    drawAscii(*display_, model.hasQuote ? formatPrice(values[i]) : "--", x + 31, y - 11, 1, UiTheme::TEXT);
  }
}

void StockScreen::drawTurnover(const StockViewModel& model) {
  const int height = StockScreenLayout::TURNOVER_Y1 - StockScreenLayout::TURNOVER_Y0 + 1;
  display_->fillRect(0, StockScreenLayout::TURNOVER_Y0, 170, height, UiTheme::BACKGROUND);
  unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  unicodeFont_->drawUTF8(6, 159, "量");
  unicodeFont_->drawUTF8(87, 159, "额");
  drawAscii(*display_, model.hasQuote && model.quote ? formatVolume(model.quote->volume) : "--", 24, 148, 1, UiTheme::TEXT);
  drawAscii(*display_, model.hasQuote && model.quote ? formatAmount(model.quote->amount) : "--", 105, 148, 1, UiTheme::TEXT);
}

void StockScreen::drawChart(const StockViewModel& model) {
  constexpr int width = StockScreenLayout::CHART_X1 - StockScreenLayout::CHART_X0 + 1;
  constexpr int height = StockScreenLayout::CHART_Y1 - StockScreenLayout::CHART_Y0 + 1;
  display_->fillRect(StockScreenLayout::CHART_X0, StockScreenLayout::CHART_Y0, width, height, UiTheme::BACKGROUND);
  display_->drawRect(StockScreenLayout::CHART_X0, StockScreenLayout::CHART_Y0, width, height, UiTheme::GRID);
  if (!model.hasQuote || !model.quote || !model.hasIntraday || !model.intraday || model.intraday->empty()) return;

  const ChartRange range = StockScreenMath::chartRange(*model.intraday, model.quote->prevClose);
  const int guideY = StockScreenMath::chartY(model.quote->prevClose, range);
  for (int x = StockScreenLayout::CHART_X0 + 1; x < StockScreenLayout::CHART_X1; x += 5) {
    display_->drawFastHLine(x, guideY, 3, UiTheme::GRID);
  }

  bool havePrevious = false;
  int previousX = 0;
  int previousY = 0;
  uint16_t previousMinute = 0;
  for (const auto& point : *model.intraday) {
    const int x = StockScreenMath::chartX(point.minuteOfDay);
    const int y = StockScreenMath::chartY(point.price, range);
    if (havePrevious) {
      const bool crossesLunch = previousMinute <= MORNING_CLOSE && point.minuteOfDay >= AFTERNOON_OPEN;
      if (!crossesLunch) display_->drawLine(previousX, previousY, x, y, UiTheme::CHART);
    }
    previousX = x;
    previousY = y;
    previousMinute = point.minuteOfDay;
    havePrevious = true;
  }
}

void StockScreen::drawFooter(const StockViewModel& model) {
  const int height = StockScreenLayout::FOOTER_Y1 - StockScreenLayout::FOOTER_Y0 + 1;
  display_->fillRect(0, StockScreenLayout::FOOTER_Y0, 170, height, UiTheme::BACKGROUND);
  char position[16];
  std::snprintf(position, sizeof(position), "%u/%u", static_cast<unsigned>(model.index + 1), static_cast<unsigned>(model.count));
  drawAscii(*display_, position, 6, 302, 1, UiTheme::TEXT);
  drawAscii(*display_, model.wifiOnline ? "WiFi" : "OFF", 43, 302, 1,
            model.wifiOnline ? UiTheme::MUTED : UiTheme::WARNING);
  drawAscii(*display_, model.provider == ProviderId::EAST_MONEY ? "EM" : "TX", 81, 302, 1, UiTheme::MUTED);
  if (!model.errorBadge.empty()) {
    unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
    unicodeFont_->setForegroundColor(UiTheme::WARNING);
    unicodeFont_->drawUTF8(108, 314, StockScreenMath::truncateUtf8(model.errorBadge, 5).c_str());
  }
}
#endif  // ARDUINO
