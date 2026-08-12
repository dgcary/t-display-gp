#include <unity.h>

#include "StockSymbol.h"

void setUp() {}
void tearDown() {}

void test_sh_symbol() {
  auto s = StockSymbol::parse("600519");
  TEST_ASSERT_TRUE(s.valid());
  TEST_ASSERT_EQUAL_STRING("600519.SH", s.canonical().c_str());
  TEST_ASSERT_EQUAL_STRING("1.600519", s.eastMoneySecId().c_str());
  TEST_ASSERT_EQUAL_STRING("sh600519", s.tencentCode().c_str());
}

void test_sz_symbol() {
  auto s = StockSymbol::parse("300750.SZ");
  TEST_ASSERT_TRUE(s.valid());
  TEST_ASSERT_EQUAL_STRING("300750.SZ", s.canonical().c_str());
  TEST_ASSERT_EQUAL_STRING("0.300750", s.eastMoneySecId().c_str());
  TEST_ASSERT_EQUAL_STRING("sz300750", s.tencentCode().c_str());
}

void test_bse_symbol() {
  auto s = StockSymbol::parse("920047");
  TEST_ASSERT_TRUE(s.valid());
  TEST_ASSERT_EQUAL_STRING("920047.BJ", s.canonical().c_str());
  TEST_ASSERT_EQUAL_STRING("0.920047", s.eastMoneySecId().c_str());
  TEST_ASSERT_EQUAL_STRING("bj920047", s.tencentCode().c_str());
}

void test_explicit_exchange_overrides_inference() {
  auto s = StockSymbol::parse("920047.SH");
  TEST_ASSERT_TRUE(s.valid());
  TEST_ASSERT_EQUAL_STRING("920047.SH", s.canonical().c_str());
  TEST_ASSERT_EQUAL_STRING("1.920047", s.eastMoneySecId().c_str());
}

void test_reject_bad_symbol() {
  TEST_ASSERT_FALSE(StockSymbol::parse("ABC").valid());
  TEST_ASSERT_FALSE(StockSymbol::parse("60051").valid());
  TEST_ASSERT_FALSE(StockSymbol::parse("123456").valid());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sh_symbol);
  RUN_TEST(test_sz_symbol);
  RUN_TEST(test_bse_symbol);
  RUN_TEST(test_explicit_exchange_overrides_inference);
  RUN_TEST(test_reject_bad_symbol);
  return UNITY_END();
}
