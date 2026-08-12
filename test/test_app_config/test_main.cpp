#include <unity.h>

#include <string>

#include "AppConfig.h"

void setUp() {}
void tearDown() {}

AppConfig validConfig() {
  AppConfig config = AppConfig::defaults();
  config.stocks = {
      {StockSymbol::parse("600519"), "贵州茅台"},
      {StockSymbol::parse("300750"), "宁德时代"},
      {StockSymbol::parse("002594"), "比亚迪"},
  };
  return config;
}

void test_defaults_and_valid_config() {
  auto defaults = AppConfig::defaults();
  TEST_ASSERT_EQUAL_UINT32(1, defaults.schemaVersion);
  TEST_ASSERT_EQUAL_UINT32(5, defaults.quoteRefreshSec);
  TEST_ASSERT_TRUE(defaults.stocks.empty());

  auto config = validConfig();
  TEST_ASSERT_TRUE(validate(config).ok());
}

void test_stock_count_must_be_three_to_five() {
  auto config = validConfig();
  config.stocks.resize(2);
  TEST_ASSERT_EQUAL(ConfigValidationError::STOCK_COUNT, validate(config).error);
  config = validConfig();
  config.stocks.push_back({StockSymbol::parse("000001"), "平安银行"});
  config.stocks.push_back({StockSymbol::parse("600036"), "招商银行"});
  config.stocks.push_back({StockSymbol::parse("601318"), "中国平安"});
  TEST_ASSERT_EQUAL(ConfigValidationError::STOCK_COUNT, validate(config).error);
}

void test_symbols_must_be_valid_and_unique_after_normalization() {
  auto config = validConfig();
  config.stocks[1].symbol = StockSymbol::parse("ABC");
  TEST_ASSERT_EQUAL(ConfigValidationError::INVALID_SYMBOL, validate(config).error);

  config = validConfig();
  config.stocks[1].symbol = StockSymbol::parse("600519.SH");
  TEST_ASSERT_EQUAL(ConfigValidationError::DUPLICATE_SYMBOL, validate(config).error);
}

void test_name_and_refresh_validation() {
  auto config = validConfig();
  config.stocks[0].displayName = std::string(31, 'x');
  TEST_ASSERT_EQUAL(ConfigValidationError::NAME_TOO_LONG, validate(config).error);

  for (uint32_t refresh : {3u, 4u, 5u}) {
    config = validConfig();
    config.quoteRefreshSec = refresh;
    TEST_ASSERT_TRUE(validate(config).ok());
  }
  config = validConfig();
  config.quoteRefreshSec = 2;
  TEST_ASSERT_EQUAL(ConfigValidationError::REFRESH_INTERVAL, validate(config).error);
}

void test_codec_round_trip_canonicalizes_symbols() {
  auto config = validConfig();
  std::string encoded;
  TEST_ASSERT_TRUE(AppConfigCodec::encode(config, encoded));

  AppConfig decoded;
  TEST_ASSERT_TRUE(AppConfigCodec::decode(encoded, decoded));
  TEST_ASSERT_TRUE(validate(decoded).ok());
  TEST_ASSERT_EQUAL_UINT32(1, decoded.schemaVersion);
  TEST_ASSERT_EQUAL_UINT32(5, decoded.quoteRefreshSec);
  TEST_ASSERT_EQUAL_UINT32(3, decoded.stocks.size());
  TEST_ASSERT_EQUAL_STRING("600519.SH", decoded.stocks[0].symbol.canonical().c_str());
  TEST_ASSERT_EQUAL_STRING("贵州茅台", decoded.stocks[0].displayName.c_str());
  TEST_ASSERT_EQUAL_STRING("300750.SZ", decoded.stocks[1].symbol.canonical().c_str());
}

void test_decode_known_json_and_reject_bad_schema_or_json() {
  const char* body = R"json({
    "schema":1,
    "quote_refresh_sec":5,
    "stocks":[
      {"symbol":"600519.SH","name":"贵州茅台"},
      {"symbol":"300750.SZ","name":"宁德时代"},
      {"symbol":"002594.SZ","name":"比亚迪"}
    ]
  })json";
  AppConfig decoded;
  TEST_ASSERT_TRUE(AppConfigCodec::decode(body, decoded));
  TEST_ASSERT_TRUE(validate(decoded).ok());

  TEST_ASSERT_FALSE(AppConfigCodec::decode("{broken", decoded));
  TEST_ASSERT_FALSE(AppConfigCodec::decode(R"json({"schema":2,"quote_refresh_sec":5,"stocks":[]})json", decoded));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_and_valid_config);
  RUN_TEST(test_stock_count_must_be_three_to_five);
  RUN_TEST(test_symbols_must_be_valid_and_unique_after_normalization);
  RUN_TEST(test_name_and_refresh_validation);
  RUN_TEST(test_codec_round_trip_canonicalizes_symbols);
  RUN_TEST(test_decode_known_json_and_reject_bad_schema_or_json);
  return UNITY_END();
}
