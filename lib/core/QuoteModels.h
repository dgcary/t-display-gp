#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "StockSymbol.h"

enum class MarketStatus {
  PRE_OPEN,
  TRADING_AM,
  LUNCH_BREAK,
  TRADING_PM,
  CLOSED,
  NON_TRADING_DAY,
  UNKNOWN
};

enum class ProviderId { EAST_MONEY, TENCENT };

struct QuoteSnapshot {
  StockSymbol symbol;
  std::string name;
  double last = 0;
  double change = 0;
  double changePercent = 0;
  double open = 0;
  double high = 0;
  double low = 0;
  double prevClose = 0;
  uint64_t volume = 0;
  double amount = 0;
  uint64_t epochSeconds = 0;
  MarketStatus marketStatus = MarketStatus::UNKNOWN;
  ProviderId provider = ProviderId::EAST_MONEY;
};

struct IntradayPoint {
  uint16_t minuteOfDay = 0;
  float price = 0;
  float averagePrice = 0;
  uint32_t volume = 0;
};

using IntradaySeries = std::vector<IntradayPoint>;
