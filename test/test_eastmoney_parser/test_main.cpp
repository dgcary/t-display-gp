#include <unity.h>

#include "EastMoneyParser.h"

void setUp() {}
void tearDown() {}

void test_parse_scaled_quote() {
  const char* body = R"json({
    "data":{"f57":"600519","f58":"贵州茅台","f43":141025,
    "f44":142100,"f45":139800,"f46":140000,"f47":123456,
    "f48":1743210000.0,"f60":140000,"f169":1025,"f170":73,"f86":1786420800}
  })json";
  QuoteSnapshot q;
  auto err = EastMoneyParser::parseQuote(body, StockSymbol::parse("600519"), q);
  TEST_ASSERT_EQUAL(ProviderError::NONE, err);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1410.25, q.last);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1421.00, q.high);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1398.00, q.low);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1400.00, q.open);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1400.00, q.prevClose);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 10.25, q.change);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.73, q.changePercent);
  TEST_ASSERT_EQUAL_UINT64(123456, q.volume);
  TEST_ASSERT_DOUBLE_WITHIN(0.1, 1743210000.0, q.amount);
  TEST_ASSERT_EQUAL_UINT64(1786420800, q.epochSeconds);
  TEST_ASSERT_EQUAL(ProviderId::EAST_MONEY, q.provider);
  TEST_ASSERT_EQUAL_STRING("贵州茅台", q.name.c_str());
}

void test_decimal_prices_are_not_scaled_again() {
  const char* body = R"json({"data":{"f57":"600519","f58":"贵州茅台","f43":1410.25,"f44":1421.0,"f45":1398.0,"f46":1400.0,"f47":1,"f48":2.0,"f60":1400.0,"f169":10.25,"f170":0.73,"f86":1786420800}})json";
  QuoteSnapshot q;
  auto err = EastMoneyParser::parseQuote(body, StockSymbol::parse("600519"), q);
  TEST_ASSERT_EQUAL(ProviderError::NONE, err);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1410.25, q.last);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 10.25, q.change);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.73, q.changePercent);
}

void test_invalid_quote_payloads_fail_closed() {
  QuoteSnapshot q;
  q.last = 999.0;
  TEST_ASSERT_EQUAL(ProviderError::MISSING_FIELD,
                    EastMoneyParser::parseQuote(R"json({"x":1})json", StockSymbol::parse("600519"), q));
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 999.0, q.last);

  TEST_ASSERT_EQUAL(ProviderError::MISSING_FIELD,
                    EastMoneyParser::parseQuote(R"json({"data":{"f58":"贵州茅台"}})json", StockSymbol::parse("600519"), q));
  const char* dashPrice = R"json({"data":{"f57":"600519","f58":"贵州茅台","f43":"-","f44":142100,"f45":139800,"f46":140000,"f47":123456,"f48":1743210000.0,"f60":140000,"f169":1025,"f170":73,"f86":1786420800}})json";
  TEST_ASSERT_EQUAL(ProviderError::PARSE,
                    EastMoneyParser::parseQuote(dashPrice, StockSymbol::parse("600519"), q));
  const char* negativePrice = R"json({"data":{"f57":"600519","f58":"贵州茅台","f43":-1,"f44":142100,"f45":139800,"f46":140000,"f47":123456,"f48":1743210000.0,"f60":140000,"f169":1025,"f170":73,"f86":1786420800}})json";
  TEST_ASSERT_EQUAL(ProviderError::PARSE,
                    EastMoneyParser::parseQuote(negativePrice, StockSymbol::parse("600519"), q));
  TEST_ASSERT_EQUAL(ProviderError::PARSE,
                    EastMoneyParser::parseQuote("{broken", StockSymbol::parse("600519"), q));
}

void test_parse_intraday_and_filter_invalid_order() {
  const char* body = R"json({"data":{"trends":[
    "2026-08-11 09:30,1400.00,1400.00,1400.00,1400.00,1200,1680000,1400.00",
    "2026-08-11 09:31,1401.20,1400.60,1401.20,1400.00,800,1120960,1400.60",
    "2026-08-11 09:31,9999.00,9999.00,9999.00,9999.00,1,1,9999.00",
    "2026-08-11 09:29,9999.00,9999.00,9999.00,9999.00,1,1,9999.00",
    "2026-08-11 12:00,9999.00,9999.00,9999.00,9999.00,1,1,9999.00",
    "2026-08-11 13:00,1402.00,1401.00,1402.00,1400.00,500,701000,1401.00"
  ]}})json";
  IntradaySeries points;
  auto err = EastMoneyParser::parseIntraday(body, StockSymbol::parse("600519"), points);
  TEST_ASSERT_EQUAL(ProviderError::NONE, err);
  TEST_ASSERT_EQUAL_UINT32(3, points.size());
  TEST_ASSERT_EQUAL_UINT16(571, points[1].minuteOfDay);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1401.20f, points[1].price);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1400.60f, points[1].averagePrice);
  TEST_ASSERT_EQUAL_UINT32(800, points[1].volume);
  TEST_ASSERT_EQUAL_UINT16(780, points[2].minuteOfDay);
}

void test_intraday_missing_or_malformed_fails() {
  IntradaySeries points = {{570, 1.0f, 1.0f, 1}};
  TEST_ASSERT_EQUAL(ProviderError::MISSING_FIELD,
                    EastMoneyParser::parseIntraday(R"json({"data":{}})json", StockSymbol::parse("600519"), points));
  TEST_ASSERT_EQUAL_UINT32(1, points.size());
  TEST_ASSERT_EQUAL(ProviderError::PARSE,
                    EastMoneyParser::parseIntraday("{broken", StockSymbol::parse("600519"), points));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_scaled_quote);
  RUN_TEST(test_decimal_prices_are_not_scaled_again);
  RUN_TEST(test_invalid_quote_payloads_fail_closed);
  RUN_TEST(test_parse_intraday_and_filter_invalid_order);
  RUN_TEST(test_intraday_missing_or_malformed_fails);
  return UNITY_END();
}
