#pragma once

#include <cstddef>
#include <cstdint>

#include "QuoteModels.h"

enum class ProviderError {
  NONE,
  NETWORK,
  HTTP_STATUS,
  BODY_TOO_LARGE,
  PARSE,
  MISSING_FIELD,
  STALE,
  UNSUPPORTED,
  CANCELLED,
  EXPIRED
};

struct ProviderDiagnostics {
  int httpStatus = 0;
  int nativeError = 0;
  int tlsError = 0;
  int32_t expectedBytes = -1;
  size_t receivedBytes = 0;
  uint32_t elapsedMs = 0;
};

class IQuoteProvider {
 public:
  virtual ~IQuoteProvider() = default;
  virtual ProviderError fetchQuote(const StockSymbol& symbol, QuoteSnapshot& out,
                                   ProviderDiagnostics* diagnostics = nullptr) = 0;
  virtual ProviderError fetchIntraday(const StockSymbol& symbol, IntradaySeries& out,
                                      ProviderDiagnostics* diagnostics = nullptr) = 0;
};
