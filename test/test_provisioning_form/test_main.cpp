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
  return f;
}

void test_builds_valid_config_and_ignores_blank_rows() {
  auto f = validFields();
  AppConfig out;
  std::string error;
  TEST_ASSERT_TRUE(ProvisioningForm::buildConfig(f, out, error));
  TEST_ASSERT_EQUAL_UINT32(3, out.stocks.size());
  TEST_ASSERT_EQUAL_STRING("600519.SH", out.stocks[0].symbol.canonical().c_str());
  TEST_ASSERT_EQUAL_STRING("贵州茅台", out.stocks[0].displayName.c_str());
  TEST_ASSERT_EQUAL_UINT32(5, out.quoteRefreshSec);
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

void test_rejects_name_without_symbol_and_invalid_refresh() {
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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_builds_valid_config_and_ignores_blank_rows);
  RUN_TEST(test_rejects_too_few_duplicate_and_invalid_symbols);
  RUN_TEST(test_rejects_name_without_symbol_and_invalid_refresh);
  return UNITY_END();
}
