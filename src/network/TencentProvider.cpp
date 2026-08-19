#include "TencentProvider.h"

#include <string>

#include "TencentIntradayParser.h"
#include "TencentParser.h"

namespace {
ProviderError mapTransport(const HttpResponse& response) {
  switch (response.error) {
    case HttpTransportError::NONE: break;
    case HttpTransportError::NETWORK: return ProviderError::NETWORK;
    case HttpTransportError::HTTP_STATUS: return ProviderError::HTTP_STATUS;
    case HttpTransportError::BODY_TOO_LARGE: return ProviderError::BODY_TOO_LARGE;
    case HttpTransportError::TRUNCATED_BODY: return ProviderError::NETWORK;
  }
  return response.statusCode == 200 ? ProviderError::NONE : ProviderError::HTTP_STATUS;
}

void copyDiagnostics(const HttpResponse& response, ProviderDiagnostics* diagnostics) {
  if (!diagnostics) return;
  diagnostics->httpStatus = response.statusCode;
  diagnostics->nativeError = response.nativeError;
  diagnostics->tlsError = response.tlsError;
  diagnostics->expectedBytes = response.expectedBytes;
  diagnostics->receivedBytes = response.receivedBytes;
  diagnostics->elapsedMs = response.elapsedMs;
}
}  // namespace

ProviderError TencentProvider::fetchQuote(const StockSymbol& symbol, QuoteSnapshot& out,
                                          ProviderDiagnostics* diagnostics) {
  if (!symbol.valid()) return ProviderError::UNSUPPORTED;
  const HttpResponse response = transport_.get("https://qt.gtimg.cn/q=" + symbol.tencentCode(), {});
  copyDiagnostics(response, diagnostics);
  const ProviderError transportError = mapTransport(response);
  if (transportError != ProviderError::NONE) return transportError;
  return TencentParser::parseQuote(response.body, symbol, out);
}

ProviderError TencentProvider::fetchIntraday(const StockSymbol& symbol, IntradaySeries& out,
                                             ProviderDiagnostics* diagnostics) {
  if (!symbol.valid()) return ProviderError::UNSUPPORTED;
  const HttpResponse response = transport_.get(
      "https://web.ifzq.gtimg.cn/appstock/app/minute/query?code=" + symbol.tencentCode(), {});
  copyDiagnostics(response, diagnostics);
  const ProviderError transportError = mapTransport(response);
  if (transportError != ProviderError::NONE) return transportError;
  return TencentIntradayParser::parse(response.body, symbol, out);
}
