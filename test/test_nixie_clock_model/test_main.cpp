#include <unity.h>

#include "NixieClockModel.h"

void test_valid_local_time_maps_to_view_model() {
  const LocalDateTime local{2026, 8, 20, 17, 42, 9, 4};
  const NixieClockViewModel model = NixieClockModel::fromLocalDateTime(local, 1000U);

  TEST_ASSERT_TRUE(model.timeValid);
  TEST_ASSERT_EQUAL_INT(2026, model.year);
  TEST_ASSERT_EQUAL_INT(8, model.month);
  TEST_ASSERT_EQUAL_INT(20, model.day);
  TEST_ASSERT_EQUAL_INT(17, model.hour);
  TEST_ASSERT_EQUAL_INT(42, model.minute);
  TEST_ASSERT_EQUAL_INT(9, model.second);
  TEST_ASSERT_EQUAL_INT(4, model.dayOfWeek);
  TEST_ASSERT_EQUAL_STRING("THU", NixieClockModel::weekdayShort(model.dayOfWeek));
}

void test_invalid_local_time_fails_closed() {
  const LocalDateTime local{0, 0, 0, 0, 0, 0, -1};
  const NixieClockViewModel model = NixieClockModel::fromLocalDateTime(local, 0U);

  TEST_ASSERT_FALSE(model.timeValid);
  TEST_ASSERT_EQUAL_INT(0, model.hour);
  TEST_ASSERT_EQUAL_STRING("---", NixieClockModel::weekdayShort(model.dayOfWeek));
}

void test_colon_blinks_in_half_second_phases() {
  TEST_ASSERT_TRUE(NixieClockModel::colonVisible(0U));
  TEST_ASSERT_TRUE(NixieClockModel::colonVisible(499U));
  TEST_ASSERT_FALSE(NixieClockModel::colonVisible(500U));
  TEST_ASSERT_FALSE(NixieClockModel::colonVisible(999U));
  TEST_ASSERT_TRUE(NixieClockModel::colonVisible(1000U));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_local_time_maps_to_view_model);
  RUN_TEST(test_invalid_local_time_fails_closed);
  RUN_TEST(test_colon_blinks_in_half_second_phases);
  return UNITY_END();
}
