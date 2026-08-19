#include <unity.h>

#include <cstdint>

#include "device/ButtonInput.h"

void setUp() {}
void tearDown() {}

void test_short_press_emits_once_on_release() {
  ButtonInput input(40, 700);
  TEST_ASSERT_EQUAL(ButtonGesture::NONE, input.update(true, 0));
  TEST_ASSERT_EQUAL(ButtonGesture::NONE, input.update(false, 10));
  TEST_ASSERT_EQUAL(ButtonGesture::NONE, input.update(false, 49));
  TEST_ASSERT_EQUAL(ButtonGesture::NONE, input.update(false, 50));
  TEST_ASSERT_EQUAL(ButtonGesture::NONE, input.update(true, 200));
  TEST_ASSERT_EQUAL(ButtonGesture::NONE, input.update(true, 239));
  TEST_ASSERT_EQUAL(ButtonGesture::SHORT_PRESS, input.update(true, 240));
  TEST_ASSERT_EQUAL(ButtonGesture::NONE, input.update(true, 241));
}

void test_long_press_fires_once_and_suppresses_short_release() {
  ButtonInput input(40, 700);
  input.update(false, 100);
  input.update(false, 140);
  TEST_ASSERT_EQUAL(ButtonGesture::NONE, input.update(false, 799));
  TEST_ASSERT_EQUAL(ButtonGesture::LONG_PRESS, input.update(false, 800));
  TEST_ASSERT_EQUAL(ButtonGesture::NONE, input.update(false, 1200));
  input.update(true, 1300);
  TEST_ASSERT_EQUAL(ButtonGesture::NONE, input.update(true, 1340));
  TEST_ASSERT_EQUAL(ButtonGesture::NONE, input.update(true, 1400));
}

void test_release_before_long_threshold_remains_short() {
  ButtonInput input(40, 700);
  input.update(false, 1000);
  input.update(false, 1040);
  input.update(true, 1690);
  TEST_ASSERT_EQUAL(ButtonGesture::SHORT_PRESS, input.update(true, 1730));
  TEST_ASSERT_EQUAL(ButtonGesture::NONE, input.update(true, 1731));
}

void test_bounce_does_not_create_false_press() {
  ButtonInput input(40, 700);
  input.update(false, 10);
  input.update(true, 20);
  input.update(false, 25);
  input.update(true, 30);
  TEST_ASSERT_EQUAL(ButtonGesture::NONE, input.update(true, 100));
}

void test_long_timing_is_wrap_safe() {
  ButtonInput input(40, 700);
  const uint32_t start = 0xFFFFFF00u;
  input.update(false, start);
  input.update(false, start + 40u);
  TEST_ASSERT_EQUAL(ButtonGesture::NONE, input.update(false, start + 699u));
  TEST_ASSERT_EQUAL(ButtonGesture::LONG_PRESS, input.update(false, start + 700u));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_short_press_emits_once_on_release);
  RUN_TEST(test_long_press_fires_once_and_suppresses_short_release);
  RUN_TEST(test_release_before_long_threshold_remains_short);
  RUN_TEST(test_bounce_does_not_create_false_press);
  RUN_TEST(test_long_timing_is_wrap_safe);
  return UNITY_END();
}
