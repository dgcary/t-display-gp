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

void test_crypto_fetches_three_assets_in_one_request() {
  FakeTransport transport;
  transport.response.error = HttpTransportError::NONE;
  transport.response.statusCode = 200;
  transport.response.body = R"json({"bitcoin":{"usd":67890.5,"usd_24h_change":1.25,"last_updated_at":1787200000},"ethereum":{"usd":3456.7,"usd_24h_change":-2.5,"last_updated_at":1787200001},"solana":{"usd":188.2,"usd_24h_change":3.75,"last_updated_at":1787200002}})json";
  CryptoProvider provider(transport);
  CryptoSnapshot out;
  CryptoDiagnostics diagnostics;
  TEST_ASSERT_EQUAL(CryptoError::NONE, provider.fetch(out, &diagnostics));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, transport.lastUrl.find("bitcoin,ethereum,solana"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, transport.lastUrl.find("include_24hr_change=true"));
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 67890.5, out.quotes[0].priceUsd);
  TEST_ASSERT_DOUBLE_WITHIN(0.01, -2.5, out.quotes[1].change24hPercent);
  TEST_ASSERT_EQUAL_UINT64(1787200002ULL, out.quotes[2].updatedEpochSeconds);
}

void test_crypto_parse_fails_closed_on_missing_asset() {
  FakeTransport transport;
  transport.response.error = HttpTransportError::NONE;
  transport.response.statusCode = 200;
  transport.response.body = R"json({"bitcoin":{"usd":10,"usd_24h_change":1,"last_updated_at":10}})json";
  CryptoProvider provider(transport);
  CryptoSnapshot out;
  out.quotes[0].priceUsd = 999.0;
  TEST_ASSERT_EQUAL(CryptoError::MISSING_FIELD, provider.fetch(out));
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 999.0, out.quotes[0].priceUsd);
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
  RUN_TEST(test_crypto_fetches_three_assets_in_one_request);
  RUN_TEST(test_crypto_parse_fails_closed_on_missing_asset);
  RUN_TEST(test_crypto_maps_transport_failure_without_parsing);
  return UNITY_END();
}
