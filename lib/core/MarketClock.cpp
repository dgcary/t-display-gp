#include "MarketClock.h"

#include <algorithm>

namespace {

constexpr int secondsOfDay(int hour, int minute, int second) {
  return hour * 3600 + minute * 60 + second;
}

bool validLocalTime(const LocalDateTime& local) {
  return local.year > 0 && local.month >= 1 && local.month <= 12 && local.day >= 1 &&
         local.day <= 31 && local.hour >= 0 && local.hour <= 23 && local.minute >= 0 &&
         local.minute <= 59 && local.second >= 0 && local.second <= 59 &&
         local.dayOfWeek >= 0 && local.dayOfWeek <= 6;
}

}  // namespace

MarketClock::MarketClock(uint32_t configuredQuoteIntervalMs)
    : quoteIntervalMs_(std::clamp<uint32_t>(configuredQuoteIntervalMs, 3000, 5000)) {}

MarketStatus MarketClock::status(const LocalDateTime& local, bool providerHasTodayData) const {
  if (!validLocalTime(local)) {
    return MarketStatus::UNKNOWN;
  }
  if (local.dayOfWeek == 0 || local.dayOfWeek == 6) {
    return MarketStatus::NON_TRADING_DAY;
  }

  const int now = secondsOfDay(local.hour, local.minute, local.second);
  constexpr int open = secondsOfDay(9, 30, 0);
  constexpr int morningClose = secondsOfDay(11, 30, 0);
  constexpr int afternoonOpen = secondsOfDay(13, 0, 0);
  constexpr int close = secondsOfDay(15, 0, 0);

  if (now < open) {
    return MarketStatus::PRE_OPEN;
  }
  if (!providerHasTodayData) {
    return MarketStatus::NON_TRADING_DAY;
  }
  if (now <= morningClose) {
    return MarketStatus::TRADING_AM;
  }
  if (now < afternoonOpen) {
    return MarketStatus::LUNCH_BREAK;
  }
  if (now <= close) {
    return MarketStatus::TRADING_PM;
  }
  return MarketStatus::CLOSED;
}

uint32_t MarketClock::recommendedQuoteIntervalMs(MarketStatus status) const {
  switch (status) {
    case MarketStatus::TRADING_AM:
    case MarketStatus::TRADING_PM:
      return quoteIntervalMs_;
    case MarketStatus::PRE_OPEN:
    case MarketStatus::LUNCH_BREAK:
      return 60000;
    case MarketStatus::CLOSED:
    case MarketStatus::NON_TRADING_DAY:
      return 300000;
    case MarketStatus::UNKNOWN:
    default:
      return 15000;
  }
}
