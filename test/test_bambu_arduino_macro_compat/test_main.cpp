#include <unity.h>

// Arduino-ESP32 exposes DISABLED as a GPIO macro. The Bambu public header must
// remain includable after Arduino headers without colliding with that macro.
#define DISABLED 0x00
#include "BambuSessionModel.h"
#undef DISABLED

void setUp() {}
void tearDown() {}

void test_bambu_session_header_is_safe_after_arduino_disabled_macro() {
  BambuSessionModel model;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(BambuSessionState::UNCONFIGURED),
                        static_cast<int>(model.state()));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bambu_session_header_is_safe_after_arduino_disabled_macro);
  return UNITY_END();
}
