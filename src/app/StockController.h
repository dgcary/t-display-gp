#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "AppConfig.h"
#include "ButtonDebouncer.h"
#include "MarketClock.h"
#include "MarketDataWorker.h"
#include "ProviderFailover.h"

struct StockCacheEntry {
  QuoteSnapshot quote;
  IntradaySeries intraday;
  bool hasQuote = false;
  bool hasIntraday = false;
  uint32_t quoteUpdatedMs = 0;
  uint32_t intradayUpdatedMs = 0;
  uint32_t lastQuoteAttemptMs = 0;
  uint32_t lastIntradayAttemptMs = 0;
  ProviderError lastError = ProviderError::NONE;
};

struct StockViewModel {
  size_t index = 0;
  size_t count = 0;
  StockSymbol symbol;
  std::string displayName;
  const QuoteSnapshot* quote = nullptr;
  const IntradaySeries* intraday = nullptr;
  bool hasQuote = false;
  bool hasIntraday = false;
  MarketStatus marketStatus = MarketStatus::UNKNOWN;
  uint32_t dataAgeSeconds = 0;
  bool wifiOnline = false;
  ProviderId provider = ProviderId::EAST_MONEY;
  std::string errorBadge;
};

class StockController {
 public:
  explicit StockController(IMarketDataQueue& queue) : queue_(queue) {}

  void begin(const AppConfig& config);
  void tick(uint32_t nowMs, const LocalDateTime& local);
  void onButton(ButtonEvent event);
  void consumeMarketResults();
  void setWifiOnline(bool online);

  const StockViewModel& viewModel() const { return view_; }
  bool takeDirtyFlag();
  bool takeFullRedrawFlag();

 private:
  struct OutstandingRequest {
    uint32_t requestId = 0;
    MarketRequestType type = MarketRequestType::QUOTE;
    StockSymbol symbol;
    ProviderId provider = ProviderId::EAST_MONEY;
  };

  bool enqueueRequest(size_t stockIndex, MarketRequestType type, ProviderId provider, uint32_t nowMs);
  bool hasOutstanding(const StockSymbol& symbol, MarketRequestType type) const;
  size_t cacheIndexFor(const StockSymbol& symbol) const;
  bool quoteIsForLocalDate(const QuoteSnapshot& quote, const LocalDateTime& local) const;
  static int dateKey(const LocalDateTime& local);
  static bool canConfirmNonTradingDay(const LocalDateTime& local);
  void scheduleForCurrent(uint32_t nowMs, bool forceStaleOnly);
  void scheduleTradingCycle(uint32_t nowMs);
  void maybeProbePrimary(uint32_t nowMs);
  void publishView();

  IMarketDataQueue& queue_;
  AppConfig config_;
  std::vector<StockCacheEntry> caches_;
  std::vector<OutstandingRequest> outstanding_;
  ProviderFailover failover_;
  MarketClock marketClock_;
  StockViewModel view_;
  size_t currentIndex_ = 0;
  size_t roundRobinIndex_ = 0;
  uint32_t nextRequestId_ = 1;
  uint32_t lastNowMs_ = 0;
  LocalDateTime lastLocal_{};
  MarketStatus marketStatus_ = MarketStatus::UNKNOWN;
  int activeDateKey_ = 0;
  int confirmedNonTradingDateKey_ = 0;
  bool wifiOnline_ = false;
  bool dirty_ = true;
  bool fullRedraw_ = true;
};
