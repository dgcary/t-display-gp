#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "StockController.h"

class TFT_eSPI;
class U8g2_for_TFT_eSPI;

namespace StockScreenLayout {
constexpr int HEADER_Y0 = 0;
constexpr int HEADER_Y1 = 28;
constexpr int PRICE_Y0 = 30;
constexpr int PRICE_Y1 = 86;
constexpr int METRICS_Y0 = 88;
constexpr int METRICS_Y1 = 142;
constexpr int TURNOVER_Y0 = 144;
constexpr int TURNOVER_Y1 = 166;
constexpr int CHART_X0 = 8;
constexpr int CHART_X1 = 162;
constexpr int CHART_Y0 = 172;
constexpr int CHART_Y1 = 292;
constexpr int FOOTER_Y0 = 296;
constexpr int FOOTER_Y1 = 319;
}  // namespace StockScreenLayout

struct ChartRange {
  double minPrice = 0;
  double maxPrice = 1;
};

namespace StockScreenMath {
ChartRange chartRange(const IntradaySeries& series, double prevClose);
int chartX(uint16_t minuteOfDay);
int chartY(double price, const ChartRange& range);
std::string truncateUtf8(const std::string& text, size_t maxCodepoints);
}  // namespace StockScreenMath

class StockScreen {
 public:
  void begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont);
  void render(const StockViewModel& model, bool fullRedraw);

 private:
  struct RenderSignature {
    std::string symbol;
    std::string displayName;
    bool hasQuote = false;
    double last = 0;
    double change = 0;
    double changePercent = 0;
    double open = 0;
    double high = 0;
    double low = 0;
    double prevClose = 0;
    uint64_t volume = 0;
    double amount = 0;
    bool hasIntraday = false;
    size_t intradaySize = 0;
    uint16_t intradayLastMinute = 0;
    float intradayLastPrice = 0;
    size_t index = 0;
    size_t count = 0;
    MarketStatus marketStatus = MarketStatus::UNKNOWN;
    bool wifiOnline = false;
    ProviderId provider = ProviderId::EAST_MONEY;
    std::string errorBadge;
  };

  RenderSignature signatureFor(const StockViewModel& model) const;
  static bool sameQuote(const RenderSignature& a, const RenderSignature& b);
  static bool sameIntraday(const RenderSignature& a, const RenderSignature& b);
  void drawHeader(const StockViewModel& model);
  void drawQuote(const StockViewModel& model);
  void drawMetrics(const StockViewModel& model);
  void drawTurnover(const StockViewModel& model);
  void drawChart(const StockViewModel& model);
  void drawFooter(const StockViewModel& model);

  TFT_eSPI* display_ = nullptr;
  U8g2_for_TFT_eSPI* unicodeFont_ = nullptr;
  bool rendered_ = false;
  RenderSignature previous_{};
};
