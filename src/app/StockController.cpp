#include "StockController.h"

#include <algorithm>
#include <ctime>
#include <limits>
#include <utility>

#include "build_config.h"

namespace {
bool isTrading(MarketStatus status) {
  return status == MarketStatus::TRADING_AM || status == MarketStatus::TRADING_PM;
}

uint32_t elapsed(uint32_t nowMs, uint32_t thenMs) {
  return static_cast<uint32_t>(nowMs - thenMs);
}
}  // namespace

void StockController::begin(const AppConfig& config) {
  config_ = config;
  if (!validate(config_).ok()) config_.stocks.clear();
  caches_.assign(config_.stocks.size(), {});
  outstanding_.clear();
  failover_ = ProviderFailover{};
  marketClock_ = MarketClock(config_.quoteRefreshSec * 1000U);
  currentIndex_ = 0;
  roundRobinIndex_ = config_.stocks.size() > 1 ? 1 : 0;
  nextRequestId_ = 1;
  marketStatus_ = MarketStatus::UNKNOWN;
  activeDateKey_ = 0;
  confirmedNonTradingDateKey_ = 0;
  dirty_ = true;
  fullRedraw_ = true;
  publishView();
}

void StockController::setWifiOnline(bool online) {
  if (wifiOnline_ == online) return;
  wifiOnline_ = online;
  dirty_ = true;
  publishView();
}

bool StockController::quoteIsForLocalDate(const QuoteSnapshot& quote, const LocalDateTime& local) const {
  if (quote.epochSeconds == 0) return true;
  const std::time_t chinaEpoch = static_cast<std::time_t>(quote.epochSeconds + 8ULL * 3600ULL);
  std::tm utc{};
  if (!gmtime_r(&chinaEpoch, &utc)) return true;
  return utc.tm_year + 1900 == local.year && utc.tm_mon + 1 == local.month && utc.tm_mday == local.day;
}

int StockController::dateKey(const LocalDateTime& local) {
  if (local.year <= 0 || local.month < 1 || local.month > 12 || local.day < 1 || local.day > 31) return 0;
  return local.year * 10000 + local.month * 100 + local.day;
}

bool StockController::canConfirmNonTradingDay(const LocalDateTime& local) {
  if (local.dayOfWeek == 0 || local.dayOfWeek == 6) return true;
  const int minute = local.hour * 60 + local.minute;
  // Give the opening feed one minute to roll from the previous session before
  // treating a successful stale quote as evidence of a weekday market holiday.
  return minute >= 9 * 60 + 31 && minute <= 15 * 60;
}

void StockController::tick(uint32_t nowMs, const LocalDateTime& local) {
  lastNowMs_ = nowMs;
  lastLocal_ = local;
  if (config_.stocks.empty()) {
    marketStatus_ = MarketStatus::UNKNOWN;
    publishView();
    return;
  }

  const int todayKey = dateKey(local);
  if (todayKey != 0 && todayKey != activeDateKey_) {
    activeDateKey_ = todayKey;
    confirmedNonTradingDateKey_ = 0;
  }

  const auto& cache = caches_[currentIndex_];
  const bool quoteIsToday = !cache.hasQuote || quoteIsForLocalDate(cache.quote, local);
  const bool todayConfirmedNonTrading = todayKey != 0 && confirmedNonTradingDateKey_ == todayKey;
  // A stale quote from yesterday is not itself proof that today is a holiday.
  // Until one stale response is observed after the opening grace minute, let
  // the normal trading scheduler probe at the configured interval.
  const bool providerHasTodayData = quoteIsToday || !todayConfirmedNonTrading;
  const MarketStatus nextStatus = marketClock_.status(local, providerHasTodayData);
  if (nextStatus != marketStatus_) {
    marketStatus_ = nextStatus;
    dirty_ = true;
  }

  if (!wifiOnline_) {
    publishView();
    return;
  }

  if (isTrading(marketStatus_)) {
    const uint32_t interval = config_.quoteRefreshSec * 1000U;
    const auto& current = caches_[currentIndex_];
    const bool due = !current.hasQuote || current.lastQuoteAttemptMs == 0 ||
                     elapsed(nowMs, current.lastQuoteAttemptMs) >= interval;
    if (due) scheduleTradingCycle(nowMs);
    if (failover_.activeProvider(nowMs) == ProviderId::EAST_MONEY &&
        (!current.hasIntraday || current.lastIntradayAttemptMs == 0 ||
         elapsed(nowMs, current.lastIntradayAttemptMs) >= BuildConfig::INTRADAY_REFRESH_MS)) {
      enqueueRequest(currentIndex_, MarketRequestType::INTRADAY, ProviderId::EAST_MONEY, nowMs);
    }
  } else {
    const uint32_t interval = marketClock_.recommendedQuoteIntervalMs(marketStatus_);
    auto& current = caches_[currentIndex_];
    if (!current.hasQuote || current.lastQuoteAttemptMs == 0 || elapsed(nowMs, current.lastQuoteAttemptMs) >= interval) {
      enqueueRequest(currentIndex_, MarketRequestType::QUOTE, failover_.activeProvider(nowMs), nowMs);
    }
  }

  maybeProbePrimary(nowMs);
  publishView();
}

void StockController::scheduleTradingCycle(uint32_t nowMs) {
  enqueueRequest(currentIndex_, MarketRequestType::QUOTE, failover_.activeProvider(nowMs), nowMs);
  if (config_.stocks.size() <= 1) return;

  for (size_t tries = 0; tries < config_.stocks.size(); ++tries) {
    const size_t candidate = roundRobinIndex_ % config_.stocks.size();
    roundRobinIndex_ = (roundRobinIndex_ + 1) % config_.stocks.size();
    if (candidate == currentIndex_) continue;
    enqueueRequest(candidate, MarketRequestType::QUOTE, failover_.activeProvider(nowMs), nowMs);
    break;  // exactly one non-current attempt per cycle; never burst all symbols.
  }
}

void StockController::scheduleForCurrent(uint32_t nowMs, bool forceStaleOnly) {
  if (config_.stocks.empty()) return;
  auto& cache = caches_[currentIndex_];
  const uint32_t quoteInterval = config_.quoteRefreshSec * 1000U;
  const bool quoteStale = !cache.hasQuote || elapsed(nowMs, cache.quoteUpdatedMs) >= quoteInterval;
  const bool intradayStale = !cache.hasIntraday || elapsed(nowMs, cache.intradayUpdatedMs) >= BuildConfig::INTRADAY_REFRESH_MS;
  if (!forceStaleOnly || quoteStale) {
    enqueueRequest(currentIndex_, MarketRequestType::QUOTE, failover_.activeProvider(nowMs), nowMs);
  }
  if (failover_.activeProvider(nowMs) == ProviderId::EAST_MONEY && (!forceStaleOnly || intradayStale)) {
    enqueueRequest(currentIndex_, MarketRequestType::INTRADAY, ProviderId::EAST_MONEY, nowMs);
  }
}

void StockController::onButton(ButtonEvent event) {
  if (config_.stocks.empty() || event == ButtonEvent::NONE) return;
  const size_t oldIndex = currentIndex_;
  if (event == ButtonEvent::PREVIOUS) {
    currentIndex_ = currentIndex_ == 0 ? config_.stocks.size() - 1 : currentIndex_ - 1;
  } else if (event == ButtonEvent::NEXT) {
    currentIndex_ = (currentIndex_ + 1) % config_.stocks.size();
  }
  if (currentIndex_ == oldIndex) return;
  fullRedraw_ = true;
  dirty_ = true;
  publishView();  // cached values appear before any network work.
  if (wifiOnline_) scheduleForCurrent(lastNowMs_, true);
}

bool StockController::hasOutstanding(const StockSymbol& symbol, MarketRequestType type) const {
  return std::any_of(outstanding_.begin(), outstanding_.end(), [&](const OutstandingRequest& request) {
    return request.type == type && request.symbol.canonical() == symbol.canonical();
  });
}

bool StockController::enqueueRequest(size_t stockIndex, MarketRequestType type, ProviderId provider, uint32_t nowMs) {
  if (stockIndex >= config_.stocks.size()) return false;
  const StockSymbol& symbol = config_.stocks[stockIndex].symbol;
  if (hasOutstanding(symbol, type)) return false;

  MarketRequest request;
  request.requestId = nextRequestId_++;
  if (nextRequestId_ == 0) nextRequestId_ = 1;
  request.type = type;
  request.symbol = symbol;
  request.provider = provider;
  if (!queue_.enqueue(request)) return false;

  outstanding_.push_back({request.requestId, type, symbol, provider});
  auto& cache = caches_[stockIndex];
  if (type == MarketRequestType::INTRADAY) cache.lastIntradayAttemptMs = nowMs;
  else cache.lastQuoteAttemptMs = nowMs;
  return true;
}

size_t StockController::cacheIndexFor(const StockSymbol& symbol) const {
  for (size_t i = 0; i < config_.stocks.size(); ++i) {
    if (config_.stocks[i].symbol.canonical() == symbol.canonical()) return i;
  }
  return config_.stocks.size();
}

void StockController::consumeMarketResults() {
  MarketResult result;
  while (queue_.tryReceive(result)) {
    const auto it = std::find_if(outstanding_.begin(), outstanding_.end(),
                                 [&](const OutstandingRequest& request) { return request.requestId == result.requestId; });
    if (it == outstanding_.end()) continue;
    const OutstandingRequest context = *it;
    outstanding_.erase(it);
    const size_t index = cacheIndexFor(context.symbol);
    if (index >= caches_.size()) continue;
    auto& cache = caches_[index];

    if (result.error != ProviderError::NONE) {
      cache.lastError = result.error;
      if (context.type == MarketRequestType::QUOTE || context.type == MarketRequestType::PRIMARY_PROBE) {
        failover_.recordFailure(context.provider, lastNowMs_);
      }
      if (index == currentIndex_) dirty_ = true;
      continue;
    }

    if (context.type == MarketRequestType::PRIMARY_PROBE) {
      failover_.recordSuccess(ProviderId::EAST_MONEY, lastNowMs_);
      if (failover_.activeProvider(lastNowMs_) != ProviderId::EAST_MONEY) continue;
      // Second successful probe returned the primary provider to service; use that fresh quote.
    } else if (context.type == MarketRequestType::QUOTE) {
      failover_.recordSuccess(context.provider, lastNowMs_);
    }

    if (context.type == MarketRequestType::INTRADAY) {
      cache.intraday = std::move(result.intraday);
      cache.hasIntraday = true;
      cache.intradayUpdatedMs = lastNowMs_;
      cache.lastError = ProviderError::NONE;
    } else {
      const bool quoteToday = quoteIsForLocalDate(result.quote, lastLocal_);
      const int resultDateKey = dateKey(lastLocal_);
      if (!quoteToday && resultDateKey != 0 && canConfirmNonTradingDay(lastLocal_)) {
        confirmedNonTradingDateKey_ = resultDateKey;
      } else if (quoteToday && resultDateKey != 0 && confirmedNonTradingDateKey_ == resultDateKey) {
        confirmedNonTradingDateKey_ = 0;
      }
      cache.quote = std::move(result.quote);
      cache.quote.marketStatus = marketStatus_;
      cache.hasQuote = true;
      cache.quoteUpdatedMs = lastNowMs_;
      cache.lastError = ProviderError::NONE;
    }
    if (index == currentIndex_) dirty_ = true;
  }
  publishView();
}

void StockController::maybeProbePrimary(uint32_t nowMs) {
  if (config_.stocks.empty() || failover_.activeProvider(nowMs) != ProviderId::TENCENT ||
      !failover_.shouldProbePrimary(nowMs)) {
    return;
  }
  if (enqueueRequest(currentIndex_, MarketRequestType::PRIMARY_PROBE, ProviderId::EAST_MONEY, nowMs)) {
    failover_.recordPrimaryProbeAttempt(nowMs);
  }
}

void StockController::publishView() {
  StockViewModel next;
  next.index = currentIndex_;
  next.count = config_.stocks.size();
  next.wifiOnline = wifiOnline_;
  next.marketStatus = marketStatus_;
  next.provider = failover_.activeProvider(lastNowMs_);

  if (!config_.stocks.empty() && currentIndex_ < config_.stocks.size()) {
    next.symbol = config_.stocks[currentIndex_].symbol;
    const auto& cache = caches_[currentIndex_];
    next.hasQuote = cache.hasQuote;
    next.hasIntraday = cache.hasIntraday;
    next.quote = cache.hasQuote ? &cache.quote : nullptr;
    next.intraday = cache.hasIntraday ? &cache.intraday : nullptr;
    next.displayName = !config_.stocks[currentIndex_].displayName.empty()
                           ? config_.stocks[currentIndex_].displayName
                           : cache.hasQuote ? cache.quote.name : next.symbol.canonical();
    next.dataAgeSeconds = cache.hasQuote ? elapsed(lastNowMs_, cache.quoteUpdatedMs) / 1000U
                                         : std::numeric_limits<uint32_t>::max();
    if (!wifiOnline_) next.errorBadge = "离线";
    else if (cache.lastError != ProviderError::NONE) next.errorBadge = "数据异常";
    else if (!cache.hasQuote) next.errorBadge = "等待数据";
  }
  view_ = std::move(next);
}

bool StockController::takeDirtyFlag() {
  const bool value = dirty_;
  dirty_ = false;
  return value;
}

bool StockController::takeFullRedrawFlag() {
  const bool value = fullRedraw_;
  fullRedraw_ = false;
  return value;
}
