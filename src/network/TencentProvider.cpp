#include "TencentProvider.h"

#include <string>

#include "TencentParser.h"

namespace {
ProviderError mapTransport(const HttpResponse& response) {
  switch (response.error) {
    case HttpTransportError::NONE: break;
    case HttpTransportError::NETWORK: return ProviderError::NETWORK;
    case HttpTransportError::HTTP_STATUS: return ProviderError::HTTP_STATUS;
    case HttpTransportError::BODY_TOO_LARGE: return ProviderError::BODY_TOO_LARGE;
  }
  return response.statusCode == 200 ? ProviderError::NONE : ProviderError::HTTP_STATUS;
}
}  // namespace

ProviderError TencentProvider::fetchQuote(const StockSymbol& symbol, QuoteSnapshot& out) {
  if (!symbol.valid()) return ProviderError::UNSUPPORTED;
  const HttpResponse response = transport_.get("https://qt.gtimg.cn/q=" + symbol.tencentCode(), {});
  const ProviderError transportError = mapTransport(response);
  if (transportError != ProviderError::NONE) return transportError;
  return TencentParser::parseQuote(response.body, symbol, out);
}

ProviderError TencentProvider::fetchIntraday(const StockSymbol&, IntradaySeries&) {
  return ProviderError::UNSUPPORTED;
}
