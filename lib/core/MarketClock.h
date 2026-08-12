#pragma once

#include <cstdint>

#include "QuoteModels.h"

struct LocalDateTime {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int dayOfWeek = -1;  // 0=Sunday, 6=Saturday
};

class MarketClock {
 public:
  explicit MarketClock(uint32_t configuredQuoteIntervalMs = 5000);

  MarketStatus status(const LocalDateTime& local, bool providerHasTodayData) const;
  uint32_t recommendedQuoteIntervalMs(MarketStatus status) const;

 private:
  uint32_t quoteIntervalMs_;
};
