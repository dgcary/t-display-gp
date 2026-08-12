#include <unity.h>
#include "ButtonDebouncer.h"

void setUp() {}
void tearDown() {}

void test_emits_once_after_stable_press() {
  ButtonDebouncer d(40);
  TEST_ASSERT_FALSE(d.update(true, 0));
  TEST_ASSERT_FALSE(d.update(false, 10));
  TEST_ASSERT_FALSE(d.update(true, 20));
  TEST_ASSERT_FALSE(d.update(false, 30));
  TEST_ASSERT_FALSE(d.update(false, 69));
  TEST_ASSERT_TRUE(d.update(false, 70));
  TEST_ASSERT_FALSE(d.update(false, 200));
}

void test_rearms_only_after_stable_release() {
  ButtonDebouncer d(40);
  d.update(false, 1); TEST_ASSERT_TRUE(d.update(false, 41));
  TEST_ASSERT_FALSE(d.update(true, 50));
  TEST_ASSERT_FALSE(d.update(true, 89));
  TEST_ASSERT_FALSE(d.update(true, 90));
  TEST_ASSERT_FALSE(d.update(false, 100));
  TEST_ASSERT_TRUE(d.update(false, 140));
}

void test_handles_millis_wraparound() {
  ButtonDebouncer d(40);
  TEST_ASSERT_FALSE(d.update(false, 0xFFFFFFF0u));
  TEST_ASSERT_TRUE(d.update(false, 24u));
}

int main(){UNITY_BEGIN();RUN_TEST(test_emits_once_after_stable_press);RUN_TEST(test_rearms_only_after_stable_release);RUN_TEST(test_handles_millis_wraparound);return UNITY_END();}
