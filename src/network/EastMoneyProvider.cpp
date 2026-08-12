#include "EastMoneyProvider.h"

#include <string>

#include "EastMoneyParser.h"

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

const HttpHeaders kEastMoneyHeaders = {{"Referer", "https://quote.eastmoney.com/"}};

}  // namespace

ProviderError EastMoneyProvider::fetchQuote(const StockSymbol& symbol, QuoteSnapshot& out) {
  if (!symbol.valid()) return ProviderError::UNSUPPORTED;
  const std::string url =
      "https://push2.eastmoney.com/api/qt/stock/get?secid=" + symbol.eastMoneySecId() +
      "&fields=f57,f58,f43,f44,f45,f46,f47,f48,f60,f86,f169,f170";
  const HttpResponse response = transport_.get(url, kEastMoneyHeaders);
  const ProviderError transportError = mapTransport(response);
  if (transportError != ProviderError::NONE) return transportError;
  return EastMoneyParser::parseQuote(response.body, symbol, out);
}

ProviderError EastMoneyProvider::fetchIntraday(const StockSymbol& symbol, IntradaySeries& out) {
  if (!symbol.valid()) return ProviderError::UNSUPPORTED;
  const std::string url =
      "https://push2his.eastmoney.com/api/qt/stock/trends2/get?secid=" + symbol.eastMoneySecId() +
      "&fields1=f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11"
      "&fields2=f51,f52,f53,f54,f55,f56,f57,f58&ndays=1&iscr=0&iscca=0";
  const HttpResponse response = transport_.get(url, kEastMoneyHeaders);
  const ProviderError transportError = mapTransport(response);
  if (transportError != ProviderError::NONE) return transportError;
  return EastMoneyParser::parseIntraday(response.body, symbol, out);
}
