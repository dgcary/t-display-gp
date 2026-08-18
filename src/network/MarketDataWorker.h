#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "IQuoteProvider.h"
#include "build_config.h"

enum class MarketRequestType { QUOTE, INTRADAY, PRIMARY_PROBE };

enum class MarketRequestPriority : uint8_t {
  CURRENT_QUOTE = 0,
  BACKGROUND_QUOTE = 1,
  PRIMARY_PROBE = 2,
  INTRADAY = 3,
  INTRADAY_RETRY = 4
};

struct MarketRequest {
  uint32_t requestId = 0;
  MarketRequestType type = MarketRequestType::QUOTE;
  StockSymbol symbol;
  ProviderId provider = ProviderId::EAST_MONEY;
  uint32_t createdMs = 0;
  uint32_t notBeforeMs = 0;
  uint8_t attempt = 1;
  MarketRequestPriority priority = MarketRequestPriority::CURRENT_QUOTE;
};

struct MarketResult {
  uint32_t requestId = 0;
  MarketRequestType type = MarketRequestType::QUOTE;
  ProviderError error = ProviderError::NONE;
  QuoteSnapshot quote;
  IntradaySeries intraday;
  ProviderId provider = ProviderId::EAST_MONEY;
  uint8_t attempt = 1;
  uint32_t queueWaitMs = 0;
  ProviderDiagnostics diagnostics;
};

namespace MarketRequestPolicy {
inline uint32_t elapsed(uint32_t nowMs, uint32_t thenMs) {
  return static_cast<uint32_t>(nowMs - thenMs);
}

inline bool timeReached(uint32_t nowMs, uint32_t targetMs) {
  return static_cast<int32_t>(nowMs - targetMs) >= 0;
}

inline bool isIntraday(const MarketRequest& request) {
  return request.type == MarketRequestType::INTRADAY;
}

inline uint32_t ttlMs(const MarketRequest& request) {
  return isIntraday(request) ? BuildConfig::INTRADAY_REQUEST_TTL_MS
                             : BuildConfig::QUOTE_REQUEST_TTL_MS;
}

inline bool expired(const MarketRequest& request, uint32_t nowMs) {
  return request.createdMs != 0 && elapsed(nowMs, request.createdMs) > ttlMs(request);
}

inline bool ready(const MarketRequest& request, uint32_t nowMs) {
  return request.notBeforeMs == 0 || timeReached(nowMs, request.notBeforeMs);
}

inline bool retryable(ProviderError error, const ProviderDiagnostics& diagnostics) {
  if (error == ProviderError::NETWORK) return true;
  if (error != ProviderError::HTTP_STATUS) return false;
  return diagnostics.httpStatus == 408 || diagnostics.httpStatus >= 500;
}

inline uint32_t retryDelayMs(uint8_t nextAttempt, uint32_t requestId) {
  uint32_t base = 0;
  if (nextAttempt == 2) base = BuildConfig::INTRADAY_RETRY_1_MS;
  else if (nextAttempt == 3) base = BuildConfig::INTRADAY_RETRY_2_MS;
  else return 0;

  const int32_t percent = static_cast<int32_t>(requestId % 41U) - 20;
  const int32_t delta = static_cast<int32_t>((base * static_cast<uint32_t>(percent < 0 ? -percent : percent)) / 100U);
  return percent < 0 ? base - static_cast<uint32_t>(delta) : base + static_cast<uint32_t>(delta);
}
}  // namespace MarketRequestPolicy

class PendingMarketRequests {
 public:
  explicit PendingMarketRequests(size_t capacity) : capacity_(capacity) { entries_.reserve(capacity); }
  bool add(const MarketRequest& request) {
    if (entries_.size() >= capacity_ || contains(request)) return false;
    entries_.push_back(request);
    return true;
  }
  void remove(const MarketRequest& request) {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const MarketRequest& entry) { return sameKey(entry, request); });
    if (it != entries_.end()) entries_.erase(it);
  }
  bool contains(const MarketRequest& request) const {
    return std::any_of(entries_.begin(), entries_.end(),
                       [&](const MarketRequest& entry) { return sameKey(entry, request); });
  }
  size_t size() const { return entries_.size(); }

 private:
  static bool sameKey(const MarketRequest& a, const MarketRequest& b) {
    return a.type == b.type && a.symbol.valid() && b.symbol.valid() &&
           a.symbol.canonical() == b.symbol.canonical();
  }
  size_t capacity_;
  std::vector<MarketRequest> entries_;
};

struct PendingAddResult {
  bool accepted = false;
  bool replaced = false;
  MarketRequest replacedRequest;
};

class PendingMarketWork {
 public:
  explicit PendingMarketWork(size_t highPriorityCapacity) : capacity_(highPriorityCapacity) {
    highPriority_.reserve(highPriorityCapacity);
  }

  PendingAddResult add(const MarketRequest& request) {
    PendingAddResult result;
    if (!request.symbol.valid()) return result;

    if (MarketRequestPolicy::isIntraday(request)) {
      if (intraday_ && sameKey(*intraday_, request)) return result;
      result.accepted = true;
      if (intraday_) {
        result.replaced = true;
        result.replacedRequest = *intraday_;
      }
      intraday_ = request;
      return result;
    }

    if (highPriority_.size() >= capacity_) return result;
    if (std::any_of(highPriority_.begin(), highPriority_.end(),
                    [&](const MarketRequest& item) { return sameKey(item, request); })) {
      return result;
    }
    highPriority_.push_back(request);
    result.accepted = true;
    return result;
  }

  bool installRetryIfEmpty(const MarketRequest& request) {
    if (!MarketRequestPolicy::isIntraday(request) || intraday_) return false;
    intraday_ = request;
    return true;
  }

  bool popNextReady(uint32_t nowMs, MarketRequest& out, std::vector<MarketRequest>& expiredRequests) {
    for (auto it = highPriority_.begin(); it != highPriority_.end();) {
      if (MarketRequestPolicy::expired(*it, nowMs)) {
        expiredRequests.push_back(*it);
        it = highPriority_.erase(it);
      } else {
        ++it;
      }
    }
    if (intraday_ && MarketRequestPolicy::expired(*intraday_, nowMs)) {
      expiredRequests.push_back(*intraday_);
      intraday_.reset();
    }

    auto best = highPriority_.end();
    for (auto it = highPriority_.begin(); it != highPriority_.end(); ++it) {
      if (!MarketRequestPolicy::ready(*it, nowMs)) continue;
      if (best == highPriority_.end() || static_cast<uint8_t>(it->priority) < static_cast<uint8_t>(best->priority) ||
          (it->priority == best->priority && MarketRequestPolicy::elapsed(best->createdMs, it->createdMs) < 0x80000000U)) {
        best = it;
      }
    }
    if (best != highPriority_.end()) {
      out = *best;
      highPriority_.erase(best);
      return true;
    }

    if (intraday_ && MarketRequestPolicy::ready(*intraday_, nowMs)) {
      out = *intraday_;
      intraday_.reset();
      return true;
    }
    return false;
  }

  size_t intradayCount() const { return intraday_ ? 1U : 0U; }
  size_t highPriorityCount() const { return highPriority_.size(); }

 private:
  static bool sameKey(const MarketRequest& a, const MarketRequest& b) {
    return a.type == b.type && a.symbol.valid() && b.symbol.valid() &&
           a.symbol.canonical() == b.symbol.canonical();
  }

  size_t capacity_;
  std::vector<MarketRequest> highPriority_;
  std::optional<MarketRequest> intraday_;
};

class IMarketDataQueue {
 public:
  virtual ~IMarketDataQueue() = default;
  virtual bool enqueue(const MarketRequest& request) = 0;
  virtual bool tryReceive(MarketResult& result) = 0;
};

class MarketDataWorker final : public IMarketDataQueue {
 public:
  MarketDataWorker();
  ~MarketDataWorker();
  MarketDataWorker(const MarketDataWorker&) = delete;
  MarketDataWorker& operator=(const MarketDataWorker&) = delete;

  bool begin();
  bool enqueue(const MarketRequest& request) override;
  bool tryReceive(MarketResult& result) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
