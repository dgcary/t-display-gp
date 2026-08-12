#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "IQuoteProvider.h"

enum class MarketRequestType { QUOTE, INTRADAY, PRIMARY_PROBE };

struct MarketRequest {
  uint32_t requestId = 0;
  MarketRequestType type = MarketRequestType::QUOTE;
  StockSymbol symbol;
  ProviderId provider = ProviderId::EAST_MONEY;
};

struct MarketResult {
  uint32_t requestId = 0;
  MarketRequestType type = MarketRequestType::QUOTE;
  ProviderError error = ProviderError::NONE;
  QuoteSnapshot quote;
  IntradaySeries intraday;
};

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
