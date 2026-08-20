#include <unity.h>

#include "TencentIntradayParser.h"

void setUp() {}
void tearDown() {}

StockSymbol maotai() { return StockSymbol::parse("600519"); }

void test_parse_tencent_intraday_filters_after_hours_and_computes_average() {
  const char* body = R"json({
    "code":0,
    "data":{
      "sh600519":{
        "data":{
          "data":[
            "0930 10.00 100 100000.00",
            "0931 10.10 130 131000.00",
            "1130 10.20 200 202000.00",
            "1300 10.20 200 202000.00",
            "1500 10.30 300 306000.00",
            "1506 10.30 301 307030.00"
          ]
        }
      }
    }
  })json";

  IntradaySeries out;
  TEST_ASSERT_EQUAL(ProviderError::NONE, TencentIntradayParser::parse(body, maotai(), out));
  TEST_ASSERT_EQUAL_UINT32(5, out.size());
  TEST_ASSERT_EQUAL_UINT16(9 * 60 + 30, out[0].minuteOfDay);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.00f, out[0].price);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.00f, out[0].averagePrice);
  TEST_ASSERT_EQUAL_UINT32(100, out[0].volume);
  TEST_ASSERT_EQUAL_UINT16(13 * 60, out[3].minuteOfDay);
  TEST_ASSERT_EQUAL_UINT16(15 * 60, out[4].minuteOfDay);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.20f, out[4].averagePrice);
}

void test_rejects_missing_symbol_or_minute_array() {
  IntradaySeries out;
  TEST_ASSERT_EQUAL(ProviderError::MISSING_FIELD,
                    TencentIntradayParser::parse(R"json({"code":0,"data":{}})json", maotai(), out));
  TEST_ASSERT_EQUAL(ProviderError::MISSING_FIELD,
                    TencentIntradayParser::parse(
                        R"json({"code":0,"data":{"sh600519":{"data":{}}}})json", maotai(), out));
}

void test_rejects_malformed_or_out_of_order_rows() {
  IntradaySeries out;
  TEST_ASSERT_EQUAL(ProviderError::PARSE,
                    TencentIntradayParser::parse(
                        R"json({"code":0,"data":{"sh600519":{"data":{"data":["0930 BAD 1 1"]}}}})json",
                        maotai(), out));
  TEST_ASSERT_EQUAL(ProviderError::PARSE,
                    TencentIntradayParser::parse(
                        R"json({"code":0,"data":{"sh600519":{"data":{"data":["0931 10.1 1 1010","0930 10.0 2 2000"]}}}})json",
                        maotai(), out));
}

void test_rejects_provider_error_and_empty_session() {
  IntradaySeries out;
  TEST_ASSERT_EQUAL(ProviderError::PARSE,
                    TencentIntradayParser::parse(R"json({"code":1,"data":{}})json", maotai(), out));
  TEST_ASSERT_EQUAL(ProviderError::MISSING_FIELD,
                    TencentIntradayParser::parse(
                        R"json({"code":0,"data":{"sh600519":{"data":{"data":["1506 10.0 1 1000"]}}}})json",
                        maotai(), out));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_tencent_intraday_filters_after_hours_and_computes_average);
  RUN_TEST(test_rejects_missing_symbol_or_minute_array);
  RUN_TEST(test_rejects_malformed_or_out_of_order_rows);
  RUN_TEST(test_rejects_provider_error_and_empty_session);
  return UNITY_END();
}
