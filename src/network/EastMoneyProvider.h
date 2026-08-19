#pragma once

#include "HttpTransport.h"
#include "IQuoteProvider.h"

class EastMoneyProvider final : public IQuoteProvider {
 public:
  explicit EastMoneyProvider(IHttpTransport& transport) : transport_(transport) {}

  ProviderError fetchQuote(const StockSymbol& symbol, QuoteSnapshot& out,
                           ProviderDiagnostics* diagnostics = nullptr) override;
  ProviderError fetchIntraday(const StockSymbol& symbol, IntradaySeries& out,
                              ProviderDiagnostics* diagnostics = nullptr) override;

 private:
  IHttpTransport& transport_;
};
