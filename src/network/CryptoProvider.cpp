#include "CryptoProvider.h"

#include <ArduinoJson.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {
constexpr char CRYPTO_URL[] =
    "https://api.binance.com/api/v3/ticker/24hr?symbols=%5B%22BTCUSDT%22,%22ETHUSDT%22,%22SOLUSDT%22%5D";
constexpr const char* SYMBOLS[] = {"BTCUSDT", "ETHUSDT", "SOLUSDT"};

void fillDiagnostics(const HttpResponse& response, CryptoDiagnostics* diagnostics) {
  if (!diagnostics) return;
  diagnostics->httpStatus = response.statusCode;
  diagnostics->nativeError = response.nativeError;
  diagnostics->tlsError = response.tlsError;
  diagnostics->expectedBytes = response.expectedBytes;
  diagnostics->receivedBytes = response.receivedBytes;
  diagnostics->elapsedMs = response.elapsedMs;
}

CryptoError mapTransportError(HttpTransportError error) {
  switch (error) {
    case HttpTransportError::NONE: return CryptoError::NONE;
    case HttpTransportError::HTTP_STATUS: return CryptoError::HTTP_STATUS;
    case HttpTransportError::BODY_TOO_LARGE: return CryptoError::BODY_TOO_LARGE;
    case HttpTransportError::NETWORK:
    case HttpTransportError::TRUNCATED_BODY: return CryptoError::NETWORK;
  }
  return CryptoError::NETWORK;
}

int symbolIndex(const char* symbol) {
  if (!symbol) return -1;
  for (int i = 0; i < 3; ++i) {
    if (std::strcmp(symbol, SYMBOLS[i]) == 0) return i;
  }
  return -1;
}

bool parseNumber(const char* text, double& out) {
  if (!text || !*text) return false;
  errno = 0;
  char* end = nullptr;
  const double value = std::strtod(text, &end);
  if (errno != 0 || end == text || *end != '\0' || !std::isfinite(value)) return false;
  out = value;
  return true;
}
}  // namespace

CryptoError CryptoProvider::fetch(CryptoSnapshot& out, CryptoDiagnostics* diagnostics) {
  const HttpHeaders headers = {{"Accept", "application/json"}};
  const HttpResponse response = transport_.get(CRYPTO_URL, headers);
  fillDiagnostics(response, diagnostics);
  const CryptoError transportError = mapTransportError(response.error);
  if (transportError != CryptoError::NONE) return transportError;

  DynamicJsonDocument doc(4096);
  const DeserializationError parseError = deserializeJson(doc, response.body);
  if (parseError) return CryptoError::PARSE;
  JsonArrayConst rows = doc.as<JsonArrayConst>();
  if (rows.size() != 3) return CryptoError::MISSING_FIELD;

  CryptoSnapshot parsed;
  bool seen[3] = {false, false, false};
  for (JsonObjectConst row : rows) {
    const char* symbol = row["symbol"] | nullptr;
    const int index = symbolIndex(symbol);
    if (index < 0 || seen[index]) return CryptoError::PARSE;
    const char* lastPrice = row["lastPrice"] | nullptr;
    const char* changePercent = row["priceChangePercent"] | nullptr;
    if (!lastPrice || !changePercent || !row["closeTime"].is<uint64_t>()) return CryptoError::MISSING_FIELD;
    double price = 0.0;
    double change = 0.0;
    if (!parseNumber(lastPrice, price) || !parseNumber(changePercent, change) || price <= 0.0 ||
        change < -100.0 || change > 1000000.0) {
      return CryptoError::PARSE;
    }
    const uint64_t closeTimeMs = row["closeTime"].as<uint64_t>();
    if (closeTimeMs == 0) return CryptoError::PARSE;
    parsed.quotes[index] = {price, change, closeTimeMs / 1000ULL};
    seen[index] = true;
  }
  for (bool value : seen) if (!value) return CryptoError::MISSING_FIELD;
  out = parsed;
  return CryptoError::NONE;
}
