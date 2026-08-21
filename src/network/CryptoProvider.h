#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "HttpTransport.h"

enum class CryptoError {
  NONE,
  NETWORK,
  HTTP_STATUS,
  BODY_TOO_LARGE,
  PARSE,
  MISSING_FIELD,
};

struct CryptoQuote {
  double priceUsdt = 0.0;
  double change24hPercent = 0.0;
  uint64_t updatedEpochSeconds = 0;
};

struct CryptoSnapshot {
  std::array<CryptoQuote, 3> quotes{};
};

struct CryptoDiagnostics {
  int httpStatus = 0;
  int nativeError = 0;
  int tlsError = 0;
  int32_t expectedBytes = -1;
  size_t receivedBytes = 0;
  uint32_t elapsedMs = 0;
};

class CryptoProvider {
 public:
  explicit CryptoProvider(IHttpTransport& transport) : transport_(transport) {}
  CryptoError fetch(CryptoSnapshot& out, CryptoDiagnostics* diagnostics = nullptr);

 private:
  IHttpTransport& transport_;
};
