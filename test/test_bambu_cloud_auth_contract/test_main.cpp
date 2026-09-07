#include <unity.h>

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
  auto submitCode = &BambuCloudClient::submitVerificationCode;
  auto requestCode = &BambuCloudClient::requestVerificationCode;
  auto submitTfa = &BambuCloudClient::submitTfaCode;
  TEST_ASSERT_NOT_NULL(reinterpret_cast<void*>(submitCode));
  TEST_ASSERT_NOT_NULL(reinterpret_cast<void*>(requestCode));
  TEST_ASSERT_NOT_NULL(reinterpret_cast<void*>(submitTfa));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_login_result_preserves_typed_verification_challenge);
  RUN_TEST(test_client_exposes_verification_submit_resend_and_tfa_methods);
  return UNITY_END();
}
