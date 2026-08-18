#include <unity.h>

#include <string>

#include "EastMoneyProvider.h"
#include "TencentProvider.h"

namespace {

class FakeTransport final : public IHttpTransport {
 public:
  HttpResponse response;
  std::string lastUrl;
  HttpHeaders lastHeaders;

  HttpResponse get(const std::string& url, const HttpHeaders& headers) override {
    lastUrl = url;
    lastHeaders = headers;
    return response;
  }
};

bool hasHeader(const HttpHeaders& headers, const char* name, const char* value) {
  for (const auto& header : headers) {
    if (header.name == name && header.value == value) return true;
  }
  return false;
}

const char* kEastMoneyQuote =
    R"json({"data":{"f57":"600519","f58":"贵州茅台","f43":141025,"f44":142100,"f45":139800,"f46":140000,"f47":123456,"f48":1743210000.0,"f60":140000,"f169":1025,"f170":73,"f86":1786420800}})json";

const char* kEastMoneyIntraday =
    R"json({"data":{"trends":["2026-08-11 09:30,1400.00,1400.00,1400.00,1400.00,1200,1680000,1400.00"]}})json";

const char* kTencentQuote =
    "v_sh600519=\"1~贵州茅台~600519~1410.25~1400.00~1401.00~123456~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~~20260811151008~10.25~0.73~1421.00~1398.00~x~123456~174321~\";";

}  // namespace

void setUp() {}
void tearDown() {}

void test_body_buffer_never_exceeds_limit() {
  HttpBodyBuffer body(5);
  TEST_ASSERT_TRUE(body.append("abc", 3));
  TEST_ASSERT_TRUE(body.append("de", 2));
  TEST_ASSERT_FALSE(body.append("f", 1));
  TEST_ASSERT_TRUE(body.overflowed());
  TEST_ASSERT_EQUAL_UINT32(5, body.body().size());
  TEST_ASSERT_EQUAL_STRING("abcde", body.body().c_str());
}

void test_http_response_carries_transport_diagnostics() {
  HttpResponse response;
  response.error = HttpTransportError::NETWORK;
  response.statusCode = 0;
  response.nativeError = -5;
  response.tlsError = -0x7280;
  response.expectedBytes = 13824;
  response.receivedBytes = 8192;
  response.elapsedMs = 2700;

  TEST_ASSERT_EQUAL(-5, response.nativeError);
  TEST_ASSERT_EQUAL(-0x7280, response.tlsError);
  TEST_ASSERT_EQUAL_INT32(13824, response.expectedBytes);
  TEST_ASSERT_EQUAL_UINT32(8192, response.receivedBytes);
  TEST_ASSERT_EQUAL_UINT32(2700, response.elapsedMs);
}

void test_eastmoney_quote_uses_exact_url_and_referer() {
  FakeTransport transport;
  transport.response = {HttpTransportError::NONE, 200, kEastMoneyQuote};
  EastMoneyProvider provider(transport);
  QuoteSnapshot quote;
  ProviderDiagnostics diagnostics;

  TEST_ASSERT_EQUAL(ProviderError::NONE, provider.fetchQuote(StockSymbol::parse("600519"), quote, &diagnostics));
  TEST_ASSERT_EQUAL_STRING(
      "https://push2.eastmoney.com/api/qt/stock/get?secid=1.600519&fields=f57,f58,f43,f44,f45,f46,f47,f48,f60,f86,f169,f170",
      transport.lastUrl.c_str());
  TEST_ASSERT_TRUE(hasHeader(transport.lastHeaders, "Referer", "https://quote.eastmoney.com/"));
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1410.25, quote.last);
  TEST_ASSERT_EQUAL(200, diagnostics.httpStatus);
}

void test_eastmoney_intraday_uses_exact_url() {
  FakeTransport transport;
  transport.response = {HttpTransportError::NONE, 200, kEastMoneyIntraday};
  EastMoneyProvider provider(transport);
  IntradaySeries series;

  TEST_ASSERT_EQUAL(ProviderError::NONE, provider.fetchIntraday(StockSymbol::parse("600519"), series));
  TEST_ASSERT_EQUAL_STRING(
      "https://push2his.eastmoney.com/api/qt/stock/trends2/get?secid=1.600519&fields1=f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11&fields2=f51,f52,f53,f54,f55,f56,f57,f58&ndays=1&iscr=0&iscca=0",
      transport.lastUrl.c_str());
  TEST_ASSERT_EQUAL_UINT32(1, series.size());
}

void test_provider_maps_transport_failures_without_parsing() {
  FakeTransport transport;
  EastMoneyProvider provider(transport);
  QuoteSnapshot quote;
  quote.last = 88.0;

  transport.response = {HttpTransportError::BODY_TOO_LARGE, 200, {}};
  TEST_ASSERT_EQUAL(ProviderError::BODY_TOO_LARGE,
                    provider.fetchQuote(StockSymbol::parse("600519"), quote));
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 88.0, quote.last);

  transport.response = {HttpTransportError::NONE, 503, {}};
  TEST_ASSERT_EQUAL(ProviderError::HTTP_STATUS,
                    provider.fetchQuote(StockSymbol::parse("600519"), quote));
}

void test_tencent_uses_exact_url_and_intraday_is_unsupported() {
  FakeTransport transport;
  transport.response = {HttpTransportError::NONE, 200, kTencentQuote};
  TencentProvider provider(transport);
  QuoteSnapshot quote;
  IntradaySeries intraday;

  TEST_ASSERT_EQUAL(ProviderError::NONE, provider.fetchQuote(StockSymbol::parse("600519"), quote));
  TEST_ASSERT_EQUAL_STRING("https://qt.gtimg.cn/q=sh600519", transport.lastUrl.c_str());
  TEST_ASSERT_EQUAL(ProviderId::TENCENT, quote.provider);
  TEST_ASSERT_EQUAL(ProviderError::UNSUPPORTED,
                    provider.fetchIntraday(StockSymbol::parse("600519"), intraday));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_body_buffer_never_exceeds_limit);
  RUN_TEST(test_http_response_carries_transport_diagnostics);
  RUN_TEST(test_eastmoney_quote_uses_exact_url_and_referer);
  RUN_TEST(test_eastmoney_intraday_uses_exact_url);
  RUN_TEST(test_provider_maps_transport_failures_without_parsing);
  RUN_TEST(test_tencent_uses_exact_url_and_intraday_is_unsupported);
  return UNITY_END();
}
