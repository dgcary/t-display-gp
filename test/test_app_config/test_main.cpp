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
  TEST_ASSERT_EQUAL_UINT32(2, defaults.schemaVersion);
  TEST_ASSERT_EQUAL_UINT32(5, defaults.quoteRefreshSec);
  TEST_ASSERT_TRUE(defaults.stocks.empty());
  TEST_ASSERT_FALSE(defaults.weather.enabled);
  TEST_ASSERT_EQUAL_UINT32(15, defaults.weather.refreshMinutes);
  TEST_ASSERT_TRUE(defaults.location.displayName.empty());
  TEST_ASSERT_EQUAL_INT32(0, defaults.location.latitudeE6);
  TEST_ASSERT_EQUAL_INT32(0, defaults.location.longitudeE6);

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

void test_weather_validation() {
  auto config = validConfig();
  config.weather.enabled = true;
  config.weather.refreshMinutes = 15;
  config.location.displayName = "昆山";
  config.location.latitudeE6 = 31385000;
  config.location.longitudeE6 = 120980000;
  TEST_ASSERT_TRUE(validate(config).ok());

  config.location.latitudeE6 = 90000001;
  TEST_ASSERT_EQUAL(ConfigValidationError::LOCATION_COORDINATES, validate(config).error);

  config = validConfig();
  config.weather.enabled = true;
  config.location.displayName = "昆山";
  config.location.latitudeE6 = 31385000;
  config.location.longitudeE6 = 180000001;
  TEST_ASSERT_EQUAL(ConfigValidationError::LOCATION_COORDINATES, validate(config).error);

  config = validConfig();
  config.weather.enabled = true;
  config.location.displayName.clear();
  config.location.latitudeE6 = 31385000;
  config.location.longitudeE6 = 120980000;
  TEST_ASSERT_EQUAL(ConfigValidationError::LOCATION_NAME, validate(config).error);

  config = validConfig();
  config.weather.enabled = true;
  config.location.displayName = "昆山";
  config.location.latitudeE6 = 31385000;
  config.location.longitudeE6 = 120980000;
  config.weather.refreshMinutes = 4;
  TEST_ASSERT_EQUAL(ConfigValidationError::WEATHER_REFRESH_INTERVAL, validate(config).error);
  config.weather.refreshMinutes = 61;
  TEST_ASSERT_EQUAL(ConfigValidationError::WEATHER_REFRESH_INTERVAL, validate(config).error);
}

void test_codec_round_trip_canonicalizes_symbols_and_weather() {
  auto config = validConfig();
  config.weather.enabled = true;
  config.weather.refreshMinutes = 20;
  config.location.displayName = "昆山";
  config.location.latitudeE6 = 31385000;
  config.location.longitudeE6 = 120980000;

  std::string encoded;
  TEST_ASSERT_TRUE(AppConfigCodec::encode(config, encoded));

  AppConfig decoded;
  uint32_t sourceSchema = 0;
  TEST_ASSERT_TRUE(AppConfigCodec::decode(encoded, decoded, &sourceSchema));
  TEST_ASSERT_TRUE(validate(decoded).ok());
  TEST_ASSERT_EQUAL_UINT32(2, sourceSchema);
  TEST_ASSERT_EQUAL_UINT32(2, decoded.schemaVersion);
  TEST_ASSERT_EQUAL_UINT32(5, decoded.quoteRefreshSec);
  TEST_ASSERT_EQUAL_UINT32(3, decoded.stocks.size());
  TEST_ASSERT_EQUAL_STRING("600519.SH", decoded.stocks[0].symbol.canonical().c_str());
  TEST_ASSERT_EQUAL_STRING("贵州茅台", decoded.stocks[0].displayName.c_str());
  TEST_ASSERT_EQUAL_STRING("300750.SZ", decoded.stocks[1].symbol.canonical().c_str());
  TEST_ASSERT_TRUE(decoded.weather.enabled);
  TEST_ASSERT_EQUAL_UINT32(20, decoded.weather.refreshMinutes);
  TEST_ASSERT_EQUAL_STRING("昆山", decoded.location.displayName.c_str());
  TEST_ASSERT_EQUAL_INT32(31385000, decoded.location.latitudeE6);
  TEST_ASSERT_EQUAL_INT32(120980000, decoded.location.longitudeE6);
}

void test_decode_schema1_migrates_to_schema2_without_losing_stocks() {
  const char* body = R"json({
    "schema":1,
    "quote_refresh_sec":4,
    "stocks":[
      {"symbol":"600519.SH","name":"贵州茅台"},
      {"symbol":"300750.SZ","name":"宁德时代"},
      {"symbol":"002594.SZ","name":"比亚迪"}
    ]
  })json";

  AppConfig decoded;
  uint32_t sourceSchema = 0;
  TEST_ASSERT_TRUE(AppConfigCodec::decode(body, decoded, &sourceSchema));
  TEST_ASSERT_EQUAL_UINT32(1, sourceSchema);
  TEST_ASSERT_EQUAL_UINT32(2, decoded.schemaVersion);
  TEST_ASSERT_EQUAL_UINT32(4, decoded.quoteRefreshSec);
  TEST_ASSERT_EQUAL_UINT32(3, decoded.stocks.size());
  TEST_ASSERT_EQUAL_STRING("600519.SH", decoded.stocks[0].symbol.canonical().c_str());
  TEST_ASSERT_EQUAL_STRING("贵州茅台", decoded.stocks[0].displayName.c_str());
  TEST_ASSERT_FALSE(decoded.weather.enabled);
  TEST_ASSERT_EQUAL_UINT32(15, decoded.weather.refreshMinutes);
  TEST_ASSERT_TRUE(validate(decoded).ok());
}

void test_decode_rejects_bad_schema_or_json() {
  AppConfig decoded;
  TEST_ASSERT_FALSE(AppConfigCodec::decode("{broken", decoded));
  TEST_ASSERT_FALSE(AppConfigCodec::decode(R"json({"schema":3,"quote_refresh_sec":5,"stocks":[]})json", decoded));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_and_valid_config);
  RUN_TEST(test_stock_count_must_be_three_to_five);
  RUN_TEST(test_symbols_must_be_valid_and_unique_after_normalization);
  RUN_TEST(test_name_and_refresh_validation);
  RUN_TEST(test_weather_validation);
  RUN_TEST(test_codec_round_trip_canonicalizes_symbols_and_weather);
  RUN_TEST(test_decode_schema1_migrates_to_schema2_without_losing_stocks);
  RUN_TEST(test_decode_rejects_bad_schema_or_json);
  return UNITY_END();
}
