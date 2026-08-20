#include <unity.h>

#include <string>

#include "CryptoProvider.h"

void setUp() {}
void tearDown() {}

class FakeTransport final : public IHttpTransport {
 public:
  HttpResponse get(const std::string& url, const HttpHeaders& headers = {}) override {
    lastUrl = url;
    lastHeaders = headers;
    return response;
  }
  HttpResponse response;
  std::string lastUrl;
  HttpHeaders lastHeaders;
};

void test_crypto_fetches_three_binance_symbols_in_one_request() {
  FakeTransport transport;
  transport.response.error = HttpTransportError::NONE;
  transport.response.statusCode = 200;
  transport.response.body = R"json([{"symbol":"ETHUSDT","priceChangePercent":"-2.50","lastPrice":"3456.70","closeTime":1787200001000},{"symbol":"BTCUSDT","priceChangePercent":"1.25","lastPrice":"67890.50","closeTime":1787200000000},{"symbol":"SOLUSDT","priceChangePercent":"3.75","lastPrice":"188.20","closeTime":1787200002000}])json";
  CryptoProvider provider(transport);
  CryptoSnapshot out;
  CryptoDiagnostics diagnostics;
  TEST_ASSERT_EQUAL(CryptoError::NONE, provider.fetch(out, &diagnostics));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, transport.lastUrl.find("data-api.binance.vision/api/v3/ticker/24hr"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, transport.lastUrl.find("BTCUSDT"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, transport.lastUrl.find("ETHUSDT"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, transport.lastUrl.find("SOLUSDT"));
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 67890.5, out.quotes[0].priceUsdt);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, -2.5, out.quotes[1].change24hPercent);
  TEST_ASSERT_EQUAL_UINT64(1787200002ULL, out.quotes[2].updatedEpochSeconds);
}

void test_crypto_parse_fails_closed_on_missing_symbol() {
  FakeTransport transport;
  transport.response.error = HttpTransportError::NONE;
  transport.response.statusCode = 200;
  transport.response.body = R"json([{"symbol":"BTCUSDT","priceChangePercent":"1","lastPrice":"10","closeTime":1000}])json";
  CryptoProvider provider(transport);
  CryptoSnapshot out;
  out.quotes[0].priceUsdt = 999.0;
  TEST_ASSERT_EQUAL(CryptoError::MISSING_FIELD, provider.fetch(out));
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 999.0, out.quotes[0].priceUsdt);
}

void test_crypto_rejects_duplicate_or_malformed_ticker() {
  FakeTransport transport;
  transport.response.error = HttpTransportError::NONE;
  transport.response.statusCode = 200;
  transport.response.body = R"json([{"symbol":"BTCUSDT","priceChangePercent":"1","lastPrice":"10","closeTime":1000},{"symbol":"BTCUSDT","priceChangePercent":"2","lastPrice":"11","closeTime":1000},{"symbol":"SOLUSDT","priceChangePercent":"3","lastPrice":"12","closeTime":1000}])json";
  CryptoProvider provider(transport);
  CryptoSnapshot out;
  TEST_ASSERT_EQUAL(CryptoError::PARSE, provider.fetch(out));
}

void test_crypto_maps_transport_failure_without_parsing() {
  FakeTransport transport;
  transport.response.error = HttpTransportError::NETWORK;
  transport.response.nativeError = -1;
  CryptoProvider provider(transport);
  CryptoSnapshot out;
  CryptoDiagnostics diagnostics;
  TEST_ASSERT_EQUAL(CryptoError::NETWORK, provider.fetch(out, &diagnostics));
  TEST_ASSERT_EQUAL_INT(-1, diagnostics.nativeError);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crypto_fetches_three_binance_symbols_in_one_request);
  RUN_TEST(test_crypto_parse_fails_closed_on_missing_symbol);
  RUN_TEST(test_crypto_rejects_duplicate_or_malformed_ticker);
  RUN_TEST(test_crypto_maps_transport_failure_without_parsing);
  return UNITY_END();
}
