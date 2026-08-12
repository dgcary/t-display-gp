#pragma once

#include "HttpTransport.h"
#include "IQuoteProvider.h"

class TencentProvider final : public IQuoteProvider {
 public:
  explicit TencentProvider(IHttpTransport& transport) : transport_(transport) {}

  ProviderError fetchQuote(const StockSymbol& symbol, QuoteSnapshot& out) override;
  ProviderError fetchIntraday(const StockSymbol& symbol, IntradaySeries& out) override;

 private:
  IHttpTransport& transport_;
};
