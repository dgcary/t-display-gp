#include "CryptoProvider.h"

#include <ArduinoJson.h>

#include <cmath>

namespace {
constexpr char CRYPTO_URL[] =
    "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,ethereum,solana&vs_currencies=usd&include_24hr_change=true&include_last_updated_at=true";
constexpr const char* IDS[] = {"bitcoin", "ethereum", "solana"};

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
}  // namespace

CryptoError CryptoProvider::fetch(CryptoSnapshot& out, CryptoDiagnostics* diagnostics) {
  const HttpHeaders headers = {{"Accept", "application/json"}};
  const HttpResponse response = transport_.get(CRYPTO_URL, headers);
  fillDiagnostics(response, diagnostics);
  const CryptoError transportError = mapTransportError(response.error);
  if (transportError != CryptoError::NONE) return transportError;

  DynamicJsonDocument doc(1536);
  const DeserializationError parseError = deserializeJson(doc, response.body);
  if (parseError) return CryptoError::PARSE;

  CryptoSnapshot parsed;
  for (size_t i = 0; i < parsed.quotes.size(); ++i) {
    JsonObjectConst object = doc[IDS[i]].as<JsonObjectConst>();
    if (object.isNull() || !object["usd"].is<double>() ||
        !object["usd_24h_change"].is<double>() || !object["last_updated_at"].is<uint64_t>()) {
      return CryptoError::MISSING_FIELD;
    }
    const double price = object["usd"].as<double>();
    const double change = object["usd_24h_change"].as<double>();
    const uint64_t updated = object["last_updated_at"].as<uint64_t>();
    if (!std::isfinite(price) || price <= 0.0 || !std::isfinite(change) || change < -100.0 ||
        change > 1000000.0 || updated == 0) {
      return CryptoError::PARSE;
    }
    parsed.quotes[i] = {price, change, updated};
  }
  out = parsed;
  return CryptoError::NONE;
}
