#include <unity.h>

#include "BambuPortalModel.h"

void setUp() {}
void tearDown() {}

BambuConfig seededConfig() {
  BambuConfig cfg;
  cfg.enabled = true;
  cfg.region = BambuRegion::US_EU;
  cfg.email = "user@example.com";
  cfg.password = "saved-password";
  cfg.accessToken = "saved-token";
  cfg.cloudUserId = "42";
  cfg.printerSerial = "01SAMPLE";
  cfg.printerName = "P1S";
  return cfg;
}

void test_blank_password_preserves_existing_secret() {
  BambuConfig existing = seededConfig();
  BambuPortalCredentials input;
  input.enabled = true;
  input.region = BambuRegion::CHINA;
  input.email = "new@example.com";
  input.password = "";
  input.rememberPassword = false;

  BambuConfig merged = mergeBambuPortalCredentials(existing, input);
  TEST_ASSERT_EQUAL_STRING("saved-password", merged.password.c_str());
  TEST_ASSERT_EQUAL_STRING("new@example.com", merged.email.c_str());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BambuRegion::CHINA), static_cast<int>(merged.region));
}

void test_new_password_can_be_used_without_persisting_it() {
  BambuConfig existing = seededConfig();
  BambuPortalCredentials input;
  input.enabled = true;
  input.region = BambuRegion::US_EU;
  input.email = existing.email;
  input.password = "one-shot-password";
  input.rememberPassword = false;

  BambuConfig merged = mergeBambuPortalCredentials(existing, input);
  TEST_ASSERT_TRUE(merged.password.empty());
  TEST_ASSERT_EQUAL_STRING("one-shot-password", effectiveBambuPortalPassword(existing, input).c_str());
}

void test_remembered_new_password_replaces_old_password() {
  BambuConfig existing = seededConfig();
  BambuPortalCredentials input;
  input.enabled = true;
  input.region = BambuRegion::US_EU;
  input.email = existing.email;
  input.password = "replacement";
  input.rememberPassword = true;

  BambuConfig merged = mergeBambuPortalCredentials(existing, input);
  TEST_ASSERT_EQUAL_STRING("replacement", merged.password.c_str());
}

void test_logout_clears_cloud_secrets_and_printer_selection_only() {
  BambuConfig existing = seededConfig();
  BambuConfig cleared = clearBambuPortalCredentials(existing);
  TEST_ASSERT_FALSE(cleared.enabled);
  TEST_ASSERT_EQUAL_STRING("user@example.com", cleared.email.c_str());
  TEST_ASSERT_TRUE(cleared.password.empty());
  TEST_ASSERT_TRUE(cleared.accessToken.empty());
  TEST_ASSERT_TRUE(cleared.cloudUserId.empty());
  TEST_ASSERT_TRUE(cleared.printerSerial.empty());
  TEST_ASSERT_TRUE(cleared.printerName.empty());
}

void test_status_exposes_only_secret_presence_booleans() {
  BambuConfig existing = seededConfig();
  BambuPortalStatus status = buildBambuPortalStatus(existing);
  TEST_ASSERT_TRUE(status.passwordSet);
  TEST_ASSERT_TRUE(status.tokenSet);
  TEST_ASSERT_EQUAL_STRING("user@example.com", status.email.c_str());
  TEST_ASSERT_EQUAL_STRING("01SAMPLE", status.printerSerial.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_blank_password_preserves_existing_secret);
  RUN_TEST(test_new_password_can_be_used_without_persisting_it);
  RUN_TEST(test_remembered_new_password_replaces_old_password);
  RUN_TEST(test_logout_clears_cloud_secrets_and_printer_selection_only);
  RUN_TEST(test_status_exposes_only_secret_presence_booleans);
  return UNITY_END();
}
