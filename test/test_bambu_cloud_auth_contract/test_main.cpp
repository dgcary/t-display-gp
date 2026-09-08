#include <unity.h>

#include <type_traits>

#include "BambuCloudClient.h"

void setUp() {}
void tearDown() {}

void test_client_exposes_only_token_identity_and_printer_discovery_calls() {
  using FetchUserIdSignature = BambuCloudUserIdResult (BambuCloudClient::*)(
      const std::string&, BambuRegion) const;
  using FetchPrintersSignature = BambuCloudPrintersResult (BambuCloudClient::*)(
      const std::string&, BambuRegion) const;

  static_assert(std::is_same_v<decltype(&BambuCloudClient::fetchUserId), FetchUserIdSignature>);
  static_assert(std::is_same_v<decltype(&BambuCloudClient::fetchPrinters), FetchPrintersSignature>);
  TEST_PASS();
}

void test_token_result_types_fail_closed_by_default() {
  BambuCloudUserIdResult user;
  BambuCloudPrintersResult printers;
  TEST_ASSERT_FALSE(user.ok());
  TEST_ASSERT_FALSE(printers.ok());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_client_exposes_only_token_identity_and_printer_discovery_calls);
  RUN_TEST(test_token_result_types_fail_closed_by_default);
  return UNITY_END();
}
