#include <unity.h>

#include "BambuPortalModel.h"

void setUp() {}
void tearDown() {}

namespace {
BambuConfig seededConfig() {
  BambuConfig cfg;
  cfg.enabled = true;
  cfg.region = BambuRegion::CHINA;
  cfg.accessToken = "saved-token";
  cfg.cloudUserId = "u_42";
  cfg.printerCount = 2;
  cfg.printers[0].serial = "01P00A123456789";
  cfg.printers[0].name = "P1S";
  cfg.printers[1].serial = "03900A987654321";
  cfg.printers[1].name = "A1 mini";
  cfg.activePrinterIndex = 0;
  return cfg;
}

BambuPortalConfigInput seededInput() {
  BambuPortalConfigInput input;
  input.enabled = true;
  input.region = BambuRegion::CHINA;
  input.printerCount = 2;
  input.printers[0].serial = "01P00A123456789";
  input.printers[0].name = "P1S";
  input.printers[1].serial = "03900A987654321";
  input.printers[1].name = "A1 mini";
  input.activePrinterSerial = "01P00A123456789";
  return input;
}
}  // namespace

void test_blank_token_preserves_existing_token_and_user_id() {
  const BambuConfig existing = seededConfig();
  BambuPortalConfigInput input = seededInput();
  input.accessToken.clear();

  const BambuConfig merged = mergeBambuPortalConfig(existing, input);
  TEST_ASSERT_EQUAL_STRING("saved-token", merged.accessToken.c_str());
  TEST_ASSERT_EQUAL_STRING("u_42", merged.cloudUserId.c_str());
}

void test_replacement_token_invalidates_old_user_id() {
  const BambuConfig existing = seededConfig();
  BambuPortalConfigInput input = seededInput();
  input.accessToken = "replacement-token";

  const BambuConfig merged = mergeBambuPortalConfig(existing, input);
  TEST_ASSERT_EQUAL_STRING("replacement-token", merged.accessToken.c_str());
  TEST_ASSERT_TRUE(merged.cloudUserId.empty());
}

void test_region_change_without_new_token_invalidates_old_identity() {
  const BambuConfig existing = seededConfig();
  BambuPortalConfigInput input = seededInput();
  input.region = BambuRegion::US_EU;
  input.accessToken.clear();

  const BambuConfig merged = mergeBambuPortalConfig(existing, input);
  TEST_ASSERT_TRUE(merged.accessToken.empty());
  TEST_ASSERT_TRUE(merged.cloudUserId.empty());
}

void test_multi_printer_rows_and_active_selection_replace_local_list() {
  const BambuConfig existing = seededConfig();
  BambuPortalConfigInput input = seededInput();
  input.printers[0].name = "Office P1S";
  input.printers[1].name = "Desk A1 mini";
  input.activePrinterSerial = "03900A987654321";

  const BambuConfig merged = mergeBambuPortalConfig(existing, input);
  TEST_ASSERT_EQUAL_UINT32(2, merged.printerCount);
  TEST_ASSERT_EQUAL_UINT32(1, merged.activePrinterIndex);
  TEST_ASSERT_EQUAL_STRING("Office P1S", merged.printers[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("Desk A1 mini", merged.printers[1].name.c_str());
  TEST_ASSERT_EQUAL_STRING("03900A987654321", activeBambuPrinter(merged)->serial.c_str());
}

void test_missing_active_serial_falls_back_to_first_configured_printer() {
  const BambuConfig existing = seededConfig();
  BambuPortalConfigInput input = seededInput();
  input.activePrinterSerial = "not-present";

  const BambuConfig merged = mergeBambuPortalConfig(existing, input);
  TEST_ASSERT_EQUAL_UINT32(0, merged.activePrinterIndex);
}

void test_clear_credentials_clears_token_identity_and_printers() {
  const BambuConfig existing = seededConfig();
  const BambuConfig cleared = clearBambuPortalCredentials(existing);
  TEST_ASSERT_FALSE(cleared.enabled);
  TEST_ASSERT_TRUE(cleared.accessToken.empty());
  TEST_ASSERT_TRUE(cleared.cloudUserId.empty());
  TEST_ASSERT_EQUAL_UINT32(0, cleared.printerCount);
  TEST_ASSERT_NULL(activeBambuPrinter(cleared));
}

void test_status_exposes_token_presence_and_active_printer_only() {
  const BambuConfig existing = seededConfig();
  const BambuPortalStatus status = buildBambuPortalStatus(existing);
  TEST_ASSERT_TRUE(status.enabled);
  TEST_ASSERT_TRUE(status.tokenSet);
  TEST_ASSERT_EQUAL_UINT32(2, status.printerCount);
  TEST_ASSERT_EQUAL_STRING("01P00A123456789", status.activePrinterSerial.c_str());
  TEST_ASSERT_EQUAL_STRING("P1S", status.activePrinterName.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_blank_token_preserves_existing_token_and_user_id);
  RUN_TEST(test_replacement_token_invalidates_old_user_id);
  RUN_TEST(test_region_change_without_new_token_invalidates_old_identity);
  RUN_TEST(test_multi_printer_rows_and_active_selection_replace_local_list);
  RUN_TEST(test_missing_active_serial_falls_back_to_first_configured_printer);
  RUN_TEST(test_clear_credentials_clears_token_identity_and_printers);
  RUN_TEST(test_status_exposes_token_presence_and_active_printer_only);
  return UNITY_END();
}
