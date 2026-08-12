#include <unity.h>

#include "TencentParser.h"

void setUp() {}
void tearDown() {}

void test_parse_tencent_snapshot() {
  const char* body =
      "v_sh600519=\"1~贵州茅台~600519~1410.25~1400.00~1401.00~123456~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~0~~20260811151008~10.25~0.73~1421.00~1398.00~x~123456~174321~\";";

  QuoteSnapshot q;
  const auto err = TencentParser::parseQuote(body, StockSymbol::parse("600519"), q);
  TEST_ASSERT_EQUAL(ProviderError::NONE, err);
  TEST_ASSERT_EQUAL_STRING("贵州茅台", q.name.c_str());
  TEST_ASSERT_EQUAL_STRING("600519.SH", q.symbol.canonical().c_str());
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1410.25, q.last);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1400.00, q.prevClose);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1401.00, q.open);
  TEST_ASSERT_EQUAL_UINT64(123456, q.volume);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1421.00, q.high);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1398.00, q.low);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 10.25, q.change);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.73, q.changePercent);
  TEST_ASSERT_DOUBLE_WITHIN(1.0, 1743210000.0, q.amount);
  TEST_ASSERT_EQUAL_UINT64(1786432208ULL, q.epochSeconds);
  TEST_ASSERT_EQUAL(ProviderId::TENCENT, q.provider);
}

void test_reject_mismatched_or_malformed_snapshot() {
  QuoteSnapshot q;
  TEST_ASSERT_EQUAL(ProviderError::STALE,
                    TencentParser::parseQuote("v_sh600000=\"1~X~600000~1\";", StockSymbol::parse("600519"), q));
  TEST_ASSERT_EQUAL(ProviderError::PARSE,
                    TencentParser::parseQuote("broken", StockSymbol::parse("600519"), q));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_tencent_snapshot);
  RUN_TEST(test_reject_mismatched_or_malformed_snapshot);
  return UNITY_END();
}
