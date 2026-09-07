#include <unity.h>

#include <string>
#include <vector>

#include "BambuCloudProtocol.h"

void setUp() {}
void tearDown() {}

void test_extract_user_id_from_jwt_payload() {
  static std::string userId;
  userId = "old";
  TEST_ASSERT_TRUE(extractBambuUserIdFromJwt("e30.eyJ1aWQiOiIxMjM0NSJ9.signature", userId));
  TEST_ASSERT_EQUAL_STRING("u_12345", userId.c_str());
  TEST_ASSERT_FALSE(extractBambuUserIdFromJwt("not-a-jwt", userId));
  TEST_ASSERT_EQUAL_STRING("u_12345", userId.c_str());
}

void test_profile_reply_extracts_string_and_numeric_user_id() {
  static std::string userId;
  TEST_ASSERT_TRUE(parseBambuProfileUserId(R"({"uidStr":"123456"})", userId));
  TEST_ASSERT_EQUAL_STRING("u_123456", userId.c_str());
  TEST_ASSERT_TRUE(parseBambuProfileUserId(R"({"uid":987654})", userId));
  TEST_ASSERT_EQUAL_STRING("u_987654", userId.c_str());
}

void test_profile_reply_fails_closed() {
  static std::string userId = "keep-me";
  TEST_ASSERT_FALSE(parseBambuProfileUserId(R"({"name":"no uid"})", userId));
  TEST_ASSERT_EQUAL_STRING("keep-me", userId.c_str());
  TEST_ASSERT_FALSE(parseBambuProfileUserId("{broken", userId));
  TEST_ASSERT_EQUAL_STRING("keep-me", userId.c_str());
}

void test_device_list_parses_bound_printers_and_fails_closed() {
  static std::vector<BambuCloudDevice> devices;
  devices = {{"sentinel", "sentinel", "sentinel"}};
  TEST_ASSERT_TRUE(parseBambuDeviceList(R"({"data":[{"dev_id":"01P00A123456789","name":"Office P1S","dev_product_name":"P1S"},{"dev_id":"03009A987654321","name":"A1 mini","dev_product_name":"A1 mini"}]})", devices));
  TEST_ASSERT_EQUAL_UINT32(2, devices.size());
  TEST_ASSERT_EQUAL_STRING("Office P1S", devices[0].name.c_str());
  const auto keep = devices;
  TEST_ASSERT_FALSE(parseBambuDeviceList(R"({"data":[{"name":"missing serial"}]})", devices));
  TEST_ASSERT_EQUAL_UINT32(keep.size(), devices.size());
}

void test_report_topic_is_bounded_to_one_device_serial() {
  TEST_ASSERT_EQUAL_STRING("device/01P00A123456789/report", bambuReportTopic("01P00A123456789").c_str());
  TEST_ASSERT_TRUE(bambuReportTopic("").empty());
  TEST_ASSERT_TRUE(bambuReportTopic("bad/#").empty());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_extract_user_id_from_jwt_payload);
  RUN_TEST(test_profile_reply_extracts_string_and_numeric_user_id);
  RUN_TEST(test_profile_reply_fails_closed);
  RUN_TEST(test_device_list_parses_bound_printers_and_fails_closed);
  RUN_TEST(test_report_topic_is_bounded_to_one_device_serial);
  return UNITY_END();
}
