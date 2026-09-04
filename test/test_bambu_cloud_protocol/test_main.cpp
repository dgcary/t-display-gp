#include <unity.h>

#include <string>
#include <vector>

#include "BambuCloudProtocol.h"

void setUp() {}
void tearDown() {}

void test_login_reply_extracts_access_token() {
  BambuLoginReply out;
  TEST_ASSERT_TRUE(parseBambuLoginReply(200, R"({"accessToken":"token-123"})", out));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BambuLoginDisposition::TOKEN),
                        static_cast<int>(out.disposition));
  TEST_ASSERT_EQUAL_STRING("token-123", out.accessToken.c_str());
  TEST_ASSERT_TRUE(out.tfaKey.empty());
}

void test_login_reply_detects_email_code_challenge() {
  BambuLoginReply out;
  TEST_ASSERT_TRUE(parseBambuLoginReply(200, R"({"loginType":"verifyCode"})", out));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BambuLoginDisposition::NEED_EMAIL_CODE),
                        static_cast<int>(out.disposition));
}

void test_login_reply_detects_tfa_by_key_when_login_type_is_empty() {
  BambuLoginReply out;
  TEST_ASSERT_TRUE(parseBambuLoginReply(200, R"({"loginType":"","tfaKey":"challenge-abc"})", out));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BambuLoginDisposition::NEED_TFA),
                        static_cast<int>(out.disposition));
  TEST_ASSERT_EQUAL_STRING("challenge-abc", out.tfaKey.c_str());
}

void test_login_reply_maps_http_error_and_malformed_input_fails_closed() {
  BambuLoginReply out;
  TEST_ASSERT_TRUE(parseBambuLoginReply(401, R"({"error":"Incorrect account or password."})", out));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BambuLoginDisposition::ERROR),
                        static_cast<int>(out.disposition));
  TEST_ASSERT_EQUAL_STRING("Incorrect account or password.", out.error.c_str());

  BambuLoginReply keep;
  keep.disposition = BambuLoginDisposition::TOKEN;
  keep.accessToken = "keep-token";
  TEST_ASSERT_FALSE(parseBambuLoginReply(200, "{broken", keep));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BambuLoginDisposition::TOKEN),
                        static_cast<int>(keep.disposition));
  TEST_ASSERT_EQUAL_STRING("keep-token", keep.accessToken.c_str());
}

void test_extract_user_id_from_jwt_payload() {
  std::string userId = "old";
  const std::string token = "e30.eyJ1aWQiOiIxMjM0NSJ9.signature";
  TEST_ASSERT_TRUE(extractBambuUserIdFromJwt(token, userId));
  TEST_ASSERT_EQUAL_STRING("u_12345", userId.c_str());

  const std::string malformed = "not-a-jwt";
  TEST_ASSERT_FALSE(extractBambuUserIdFromJwt(malformed, userId));
  TEST_ASSERT_EQUAL_STRING("u_12345", userId.c_str());
}

void test_device_list_parses_bound_printers_and_fails_closed() {
  const std::string body = R"({"data":[{"dev_id":"01P00A123456789","name":"Office P1S","dev_product_name":"P1S"},{"dev_id":"03009A987654321","name":"A1 mini","dev_product_name":"A1 mini"}]})";
  std::vector<BambuCloudDevice> devices;
  TEST_ASSERT_TRUE(parseBambuDeviceList(body, devices));
  TEST_ASSERT_EQUAL_UINT32(2, devices.size());
  TEST_ASSERT_EQUAL_STRING("01P00A123456789", devices[0].serial.c_str());
  TEST_ASSERT_EQUAL_STRING("Office P1S", devices[0].name.c_str());
  TEST_ASSERT_EQUAL_STRING("P1S", devices[0].model.c_str());

  const auto keep = devices;
  TEST_ASSERT_FALSE(parseBambuDeviceList(R"({"data":[{"name":"missing serial"}]})", devices));
  TEST_ASSERT_EQUAL_UINT32(keep.size(), devices.size());
  TEST_ASSERT_EQUAL_STRING(keep[0].serial.c_str(), devices[0].serial.c_str());
}

void test_report_topic_is_bounded_to_one_device_serial() {
  TEST_ASSERT_EQUAL_STRING("device/01P00A123456789/report",
                           bambuReportTopic("01P00A123456789").c_str());
  TEST_ASSERT_TRUE(bambuReportTopic("").empty());
  TEST_ASSERT_TRUE(bambuReportTopic("bad/#").empty());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_login_reply_extracts_access_token);
  RUN_TEST(test_login_reply_detects_email_code_challenge);
  RUN_TEST(test_login_reply_detects_tfa_by_key_when_login_type_is_empty);
  RUN_TEST(test_login_reply_maps_http_error_and_malformed_input_fails_closed);
  RUN_TEST(test_extract_user_id_from_jwt_payload);
  RUN_TEST(test_device_list_parses_bound_printers_and_fails_closed);
  RUN_TEST(test_report_topic_is_bounded_to_one_device_serial);
  return UNITY_END();
}
