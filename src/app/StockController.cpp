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

bool utcTime(std::time_t value, std::tm& out) {
#if defined(_WIN32)
  return ::gmtime_s(&out, &value) == 0;
#else
  return ::gmtime_r(&value, &out) != nullptr;
#endif
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
  if (!utcTime(chinaEpoch, utc)) return true;
  return utc.tm_year + 1900 == local.year && utc.tm_mon + 1 == local.month && utc.tm_mday == local.day;
}

int StockController::dateKey(const LocalDateTime& local) {
  if (local.year <= 0 || local.month < 1 || local.month > 12 || local.day < 1 || local.day > 31) return 0;
  return local.year * 10000 + local.month * 100 + local.day;
}

bool StockController::canConfirmNonTradingDay(const LocalDateTime& local) {
  if (local.dayOfWeek == 0 || local.dayOfWeek == 6) return true;
  const int minute = local.hour * 60 + local.minute;
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
    const bool due = !current.hasQuote || current.quoteHealth.lastAttemptMs == 0 ||
                     elapsed(nowMs, current.quoteHealth.lastAttemptMs) >= interval;
    if (due) scheduleTradingCycle(nowMs);
    if (!current.hasIntraday || current.intradayHealth.lastAttemptMs == 0 ||
        elapsed(nowMs, current.intradayHealth.lastAttemptMs) >= BuildConfig::INTRADAY_REFRESH_MS) {
      // Intraday provider health is independent of quote failover. Each new
      // cycle starts with EastMoney; MarketDataWorker falls back to Tencent
      // only if the EastMoney trend request cannot complete successfully.
      enqueueRequest(currentIndex_, MarketRequestType::INTRADAY, ProviderId::EAST_MONEY, nowMs);
    }
  } else {
    const uint32_t interval = marketClock_.recommendedQuoteIntervalMs(marketStatus_);
    auto& current = caches_[currentIndex_];
    if (!current.hasQuote || current.quoteHealth.lastAttemptMs == 0 ||
        elapsed(nowMs, current.quoteHealth.lastAttemptMs) >= interval) {
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
    break;
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
  if (!forceStaleOnly || intradayStale) {
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
  publishView();
  if (wifiOnline_) scheduleForCurrent(lastNowMs_, true);
}

bool StockController::hasOutstandingAtOrAbove(const StockSymbol& symbol, MarketRequestType type,
                                              MarketRequestPriority priority) const {
  return std::any_of(outstanding_.begin(), outstanding_.end(), [&](const OutstandingRequest& request) {
    return request.type == type && request.symbol.canonical() == symbol.canonical() &&
           static_cast<uint8_t>(request.priority) <= static_cast<uint8_t>(priority);
  });
}

bool StockController::enqueueRequest(size_t stockIndex, MarketRequestType type, ProviderId provider, uint32_t nowMs) {
  if (stockIndex >= config_.stocks.size()) return false;
  const StockSymbol& symbol = config_.stocks[stockIndex].symbol;

  MarketRequestPriority priority = MarketRequestPriority::CURRENT_QUOTE;
  if (type == MarketRequestType::INTRADAY) {
    priority = MarketRequestPriority::INTRADAY;
  } else if (type == MarketRequestType::PRIMARY_PROBE) {
    priority = MarketRequestPriority::PRIMARY_PROBE;
  } else {
    priority = stockIndex == currentIndex_ ? MarketRequestPriority::CURRENT_QUOTE
                                           : MarketRequestPriority::BACKGROUND_QUOTE;
  }
  if (hasOutstandingAtOrAbove(symbol, type, priority)) return false;

  MarketRequest request;
  request.requestId = nextRequestId_++;
  if (nextRequestId_ == 0) nextRequestId_ = 1;
  request.type = type;
  request.symbol = symbol;
  request.provider = provider;
  request.createdMs = nowMs;
  request.notBeforeMs = nowMs;
  request.cycleStartedMs = nowMs;
  request.attempt = 1;
  request.priority = priority;
  if (!queue_.enqueue(request)) return false;

  outstanding_.push_back({request.requestId, type, symbol, provider, priority});
  auto& cache = caches_[stockIndex];
  if (type == MarketRequestType::INTRADAY) cache.intradayHealth.lastAttemptMs = nowMs;
  else if (type == MarketRequestType::QUOTE) cache.quoteHealth.lastAttemptMs = nowMs;
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

    if (result.error == ProviderError::CANCELLED || result.error == ProviderError::EXPIRED) {
      if (index == currentIndex_) dirty_ = true;
      continue;
    }

    if (result.error != ProviderError::NONE) {
      if (context.type == MarketRequestType::INTRADAY) {
        cache.intradayHealth.lastError = result.error;
        ++cache.intradayHealth.consecutiveFailures;
      } else if (context.type == MarketRequestType::QUOTE) {
        cache.quoteHealth.lastError = result.error;
        ++cache.quoteHealth.consecutiveFailures;
        failover_.recordFailure(context.provider, lastNowMs_);
      } else {
        failover_.recordFailure(context.provider, lastNowMs_);
      }
      if (index == currentIndex_) dirty_ = true;
      continue;
    }

    if (context.type == MarketRequestType::PRIMARY_PROBE) {
      failover_.recordSuccess(ProviderId::EAST_MONEY, lastNowMs_);
      if (failover_.activeProvider(lastNowMs_) != ProviderId::EAST_MONEY) continue;
    } else if (context.type == MarketRequestType::QUOTE) {
      failover_.recordSuccess(context.provider, lastNowMs_);
    }

    if (context.type == MarketRequestType::INTRADAY) {
      cache.intraday = std::move(result.intraday);
      cache.hasIntraday = true;
      cache.intradayProvider = result.provider;
      cache.intradayUpdatedMs = lastNowMs_;
      cache.intradayHealth.lastSuccessMs = lastNowMs_;
      cache.intradayHealth.lastError = ProviderError::NONE;
      cache.intradayHealth.consecutiveFailures = 0;
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
      cache.quoteHealth.lastSuccessMs = lastNowMs_;
      cache.quoteHealth.lastError = ProviderError::NONE;
      cache.quoteHealth.consecutiveFailures = 0;
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
    next.intradayProvider = cache.intradayProvider;
    next.displayName = !config_.stocks[currentIndex_].displayName.empty()
                           ? config_.stocks[currentIndex_].displayName
                           : cache.hasQuote ? cache.quote.name : next.symbol.canonical();
    next.quoteAgeSeconds = cache.hasQuote ? elapsed(lastNowMs_, cache.quoteUpdatedMs) / 1000U
                                          : std::numeric_limits<uint32_t>::max();
    next.intradayAgeSeconds = cache.hasIntraday ? elapsed(lastNowMs_, cache.intradayUpdatedMs) / 1000U
                                                : std::numeric_limits<uint32_t>::max();
    next.dataAgeSeconds = next.quoteAgeSeconds;
    next.quoteError = cache.quoteHealth.lastError;
    next.intradayError = cache.intradayHealth.lastError;
    const bool tradingNow = isTrading(marketStatus_);
    next.quoteDelayed = tradingNow && cache.hasQuote &&
        (cache.quoteHealth.consecutiveFailures >= BuildConfig::CHANNEL_DELAY_FAILURE_CYCLES ||
         elapsed(lastNowMs_, cache.quoteUpdatedMs) >= BuildConfig::QUOTE_DELAY_MS);
    next.intradayDelayed = tradingNow &&
        ((cache.hasIntraday &&
          (cache.intradayHealth.consecutiveFailures >= BuildConfig::CHANNEL_DELAY_FAILURE_CYCLES ||
           elapsed(lastNowMs_, cache.intradayUpdatedMs) >= BuildConfig::INTRADAY_DELAY_MS)) ||
         (!cache.hasIntraday &&
          cache.intradayHealth.consecutiveFailures >= BuildConfig::CHANNEL_DELAY_FAILURE_CYCLES));

    if (!wifiOnline_) next.errorBadge = "离线";
    else if (!cache.hasQuote) next.errorBadge = "等待报价";
    else if (next.quoteDelayed) next.errorBadge = "报价延迟";
    else if (next.intradayDelayed) next.errorBadge = "分时延迟";
  }

  const bool healthChanged = next.errorBadge != view_.errorBadge ||
                             next.quoteDelayed != view_.quoteDelayed ||
                             next.intradayDelayed != view_.intradayDelayed ||
                             next.quoteError != view_.quoteError ||
                             next.intradayError != view_.intradayError;
  if (healthChanged) dirty_ = true;
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