#pragma once

#include "QuoteModels.h"

enum class ProviderError {
  NONE,
  NETWORK,
  HTTP_STATUS,
  BODY_TOO_LARGE,
  PARSE,
  MISSING_FIELD,
  STALE,
  UNSUPPORTED
};

class IQuoteProvider {
 public:
  virtual ~IQuoteProvider() = default;
  virtual ProviderError fetchQuote(const StockSymbol& symbol, QuoteSnapshot& out) = 0;
  virtual ProviderError fetchIntraday(const StockSymbol& symbol, IntradaySeries& out) = 0;
};
