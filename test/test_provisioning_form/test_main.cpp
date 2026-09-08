#include <unity.h>

#include "ProvisioningForm.h"

void setUp() {}
void tearDown() {}

ProvisioningFields validFields() {
  ProvisioningFields f;
  f.symbols[0] = "600519"; f.names[0] = "贵州茅台";
  f.symbols[1] = "000001"; f.names[1] = "平安银行";
  f.symbols[2] = "300750"; f.names[2] = "宁德时代";
  f.refresh = "5";
  f.locationName = "昆山";
  f.latitude = "31.385000";
  f.longitude = "120.980000";
  f.weatherEnabled = "1";
  f.weatherRefresh = "15";
  return f;
}

void test_builds_valid_config_and_weather() {
  auto f = validFields();
  AppConfig out;
  std::string error;
  TEST_ASSERT_TRUE(ProvisioningForm::buildConfig(f, out, error));
  TEST_ASSERT_EQUAL_UINT32(3, out.stocks.size());
  TEST_ASSERT_EQUAL_STRING("600519.SH", out.stocks[0].symbol.canonical().c_str());
  TEST_ASSERT_EQUAL_STRING("贵州茅台", out.stocks[0].displayName.c_str());
  TEST_ASSERT_EQUAL_UINT32(5, out.quoteRefreshSec);
  TEST_ASSERT_TRUE(out.weather.enabled);
  TEST_ASSERT_EQUAL_UINT32(15, out.weather.refreshMinutes);
  TEST_ASSERT_EQUAL_STRING("昆山", out.location.displayName.c_str());
  TEST_ASSERT_EQUAL_INT32(31385000, out.location.latitudeE6);
  TEST_ASSERT_EQUAL_INT32(120980000, out.location.longitudeE6);
}

void test_weather_can_be_disabled_without_location() {
  auto f = validFields();
  f.weatherEnabled = "0";
  f.locationName.clear();
  f.latitude.clear();
  f.longitude.clear();
  AppConfig out;
  std::string error;
  TEST_ASSERT_TRUE(ProvisioningForm::buildConfig(f, out, error));
  TEST_ASSERT_FALSE(out.weather.enabled);
  TEST_ASSERT_TRUE(out.location.displayName.empty());
}

void test_rejects_too_few_duplicate_and_invalid_symbols() {
  AppConfig out;
  std::string error;
  auto f = validFields();
  f.symbols[2].clear(); f.names[2].clear();
  TEST_ASSERT_FALSE(ProvisioningForm::buildConfig(f, out, error));
  TEST_ASSERT_TRUE(error.find("3") != std::string::npos);

  f = validFields();
  f.symbols[2] = "600519.SH";
  TEST_ASSERT_FALSE(ProvisioningForm::buildConfig(f, out, error));

  f = validFields();
  f.symbols[1] = "ABC";
  TEST_ASSERT_FALSE(ProvisioningForm::buildConfig(f, out, error));
}

void test_rejects_name_without_symbol_and_invalid_stock_refresh() {
  AppConfig out;
  std::string error;
  auto f = validFields();
  f.symbols[3].clear(); f.names[3] = "孤立名称";
  TEST_ASSERT_FALSE(ProvisioningForm::buildConfig(f, out, error));

  f = validFields();
  f.refresh = "6";
  TEST_ASSERT_FALSE(ProvisioningForm::buildConfig(f, out, error));

  f.refresh = "4";
  TEST_ASSERT_TRUE(ProvisioningForm::buildConfig(f, out, error));
}

void test_rejects_invalid_weather_location_and_refresh() {
  AppConfig out;
  std::string error;
  auto f = validFields();
  f.latitude = "91.0";
  TEST_ASSERT_FALSE(ProvisioningForm::buildConfig(f, out, error));

  f = validFields();
  f.longitude = "-181";
  TEST_ASSERT_FALSE(ProvisioningForm::buildConfig(f, out, error));

  f = validFields();
  f.latitude = "31.2oops";
  TEST_ASSERT_FALSE(ProvisioningForm::buildConfig(f, out, error));

  f = validFields();
  f.locationName.clear();
  TEST_ASSERT_FALSE(ProvisioningForm::buildConfig(f, out, error));

  f = validFields();
  f.weatherRefresh = "4";
  TEST_ASSERT_FALSE(ProvisioningForm::buildConfig(f, out, error));

  f = validFields();
  f.weatherRefresh = "61";
  TEST_ASSERT_FALSE(ProvisioningForm::buildConfig(f, out, error));
}

void test_from_config_round_trips_weather_fields() {
  AppConfig config = AppConfig::defaults();
  config.stocks = {
      {StockSymbol::parse("600519"), "贵州茅台"},
      {StockSymbol::parse("000001"), "平安银行"},
      {StockSymbol::parse("300750"), "宁德时代"},
  };
  config.quoteRefreshSec = 4;
  config.weather.enabled = true;
  config.weather.refreshMinutes = 20;
  config.location.displayName = "昆山";
  config.location.latitudeE6 = 31385000;
  config.location.longitudeE6 = 120980000;

  const ProvisioningFields fields = ProvisioningForm::fromConfig(config);
  TEST_ASSERT_EQUAL_STRING("4", fields.refresh.c_str());
  TEST_ASSERT_EQUAL_STRING("昆山", fields.locationName.c_str());
  TEST_ASSERT_EQUAL_STRING("31.385000", fields.latitude.c_str());
  TEST_ASSERT_EQUAL_STRING("120.980000", fields.longitude.c_str());
  TEST_ASSERT_EQUAL_STRING("1", fields.weatherEnabled.c_str());
  TEST_ASSERT_EQUAL_STRING("20", fields.weatherRefresh.c_str());

  AppConfig decoded;
  std::string error;
  TEST_ASSERT_TRUE(ProvisioningForm::buildConfig(fields, decoded, error));
  TEST_ASSERT_EQUAL_INT32(config.location.latitudeE6, decoded.location.latitudeE6);
  TEST_ASSERT_EQUAL_INT32(config.location.longitudeE6, decoded.location.longitudeE6);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_builds_valid_config_and_weather);
  RUN_TEST(test_weather_can_be_disabled_without_location);
  RUN_TEST(test_rejects_too_few_duplicate_and_invalid_symbols);
  RUN_TEST(test_rejects_name_without_symbol_and_invalid_stock_refresh);
  RUN_TEST(test_rejects_invalid_weather_location_and_refresh);
  RUN_TEST(test_from_config_round_trips_weather_fields);
  return UNITY_END();
}
