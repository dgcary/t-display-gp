#include <unity.h>

#include <type_traits>

#include "BambuCloudClient.h"

void setUp() {}
void tearDown() {}

void test_login_result_preserves_typed_verification_challenge() {
  BambuCloudLoginResult result;
  result.error = BambuCloudError::VERIFICATION_REQUIRED;
  result.verificationType = BambuVerificationType::SMS_CODE;
  result.httpStatus = 200;

  TEST_ASSERT_FALSE(result.ok());
  TEST_ASSERT_TRUE(result.verificationRequired());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BambuVerificationType::SMS_CODE),
                        static_cast<int>(result.verificationType));
  TEST_ASSERT_EQUAL_INT(200, result.httpStatus);
}

void test_client_exposes_verification_submit_resend_and_tfa_methods() {
  using SubmitVerificationSignature = BambuCloudLoginResult (BambuCloudClient::*)(
      const std::string&, const std::string&, BambuRegion) const;
  using RequestVerificationSignature = BambuCloudError (BambuCloudClient::*)(
      const std::string&, BambuRegion) const;
  using SubmitTfaSignature = BambuCloudLoginResult (BambuCloudClient::*)(
      const std::string&, const std::string&, BambuRegion) const;

  static_assert(std::is_same_v<decltype(&BambuCloudClient::submitVerificationCode),
                               SubmitVerificationSignature>);
  static_assert(std::is_same_v<decltype(&BambuCloudClient::requestVerificationCode),
                               RequestVerificationSignature>);
  static_assert(std::is_same_v<decltype(&BambuCloudClient::submitTfaCode),
                               SubmitTfaSignature>);
  TEST_PASS();
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_login_result_preserves_typed_verification_challenge);
  RUN_TEST(test_client_exposes_verification_submit_resend_and_tfa_methods);
  return UNITY_END();
}
