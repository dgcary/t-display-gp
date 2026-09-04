#include <unity.h>

#include <string>

#include "BambuState.h"

void setUp() {}
void tearDown() {}

void test_running_report_populates_core_print_state() {
  BambuState state;
  state.connected = true;

  const char* payload = R"({"print":{"gcode_state":"RUNNING","mc_percent":42,"mc_remaining_time":73,"nozzle_temper":220.5,"nozzle_target_temper":220,"bed_temper":60.2,"bed_target_temper":60,"chamber_temper":36,"layer_num":123,"total_layer_num":400,"subtask_name":"gearbox_cover"}})";

  TEST_ASSERT_TRUE(applyBambuReport(payload, 1234U, state));
  TEST_ASSERT_TRUE(state.connected);
  TEST_ASSERT_EQUAL(BambuPrintState::RUNNING, state.printState);
  TEST_ASSERT_EQUAL_STRING("RUNNING", state.gcodeState.c_str());
  TEST_ASSERT_TRUE(state.printing);
  TEST_ASSERT_EQUAL_UINT8(42U, state.progress);
  TEST_ASSERT_EQUAL_UINT16(73U, state.remainingMinutes);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 220.5f, state.nozzleTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 220.0f, state.nozzleTarget);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.2f, state.bedTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, state.bedTarget);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 36.0f, state.chamberTemp);
  TEST_ASSERT_EQUAL_UINT16(123U, state.layerNum);
  TEST_ASSERT_EQUAL_UINT16(400U, state.totalLayers);
  TEST_ASSERT_EQUAL_STRING("gearbox_cover", state.jobName.c_str());
  TEST_ASSERT_EQUAL_UINT32(1234U, state.lastUpdateMs);
}

void test_pause_and_finish_states_are_normalized() {
  BambuState state;
  TEST_ASSERT_TRUE(applyBambuReport(R"({"print":{"gcode_state":"PAUSE","mc_percent":64}})", 10U, state));
  TEST_ASSERT_EQUAL(BambuPrintState::PAUSE, state.printState);
  TEST_ASSERT_TRUE(state.printing);
  TEST_ASSERT_EQUAL_UINT8(64U, state.progress);

  TEST_ASSERT_TRUE(applyBambuReport(R"({"print":{"gcode_state":"FINISH","mc_percent":100,"mc_remaining_time":0}})", 20U, state));
  TEST_ASSERT_EQUAL(BambuPrintState::FINISH, state.printState);
  TEST_ASSERT_FALSE(state.printing);
  TEST_ASSERT_EQUAL_UINT8(100U, state.progress);
  TEST_ASSERT_EQUAL_UINT16(0U, state.remainingMinutes);
  TEST_ASSERT_EQUAL_UINT32(20U, state.lastUpdateMs);
}

void test_partial_delta_preserves_absent_fields() {
  BambuState state;
  state.progress = 31U;
  state.remainingMinutes = 88U;
  state.nozzleTemp = 215.0f;
  state.bedTemp = 55.0f;
  state.jobName = "keep-this-job";
  state.layerNum = 12U;
  state.totalLayers = 200U;

  TEST_ASSERT_TRUE(applyBambuReport(R"({"print":{"nozzle_temper":218.5,"layer_num":13}})", 500U, state));
  TEST_ASSERT_EQUAL_UINT8(31U, state.progress);
  TEST_ASSERT_EQUAL_UINT16(88U, state.remainingMinutes);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 218.5f, state.nozzleTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 55.0f, state.bedTemp);
  TEST_ASSERT_EQUAL_STRING("keep-this-job", state.jobName.c_str());
  TEST_ASSERT_EQUAL_UINT16(13U, state.layerNum);
  TEST_ASSERT_EQUAL_UINT16(200U, state.totalLayers);
  TEST_ASSERT_EQUAL_UINT32(500U, state.lastUpdateMs);
}

void test_malformed_json_fails_without_mutating_state() {
  BambuState state;
  state.connected = true;
  state.gcodeState = "RUNNING";
  state.printState = BambuPrintState::RUNNING;
  state.printing = true;
  state.progress = 77U;
  state.remainingMinutes = 15U;
  state.nozzleTemp = 225.0f;
  state.jobName = "keep";
  state.lastUpdateMs = 900U;

  TEST_ASSERT_FALSE(applyBambuReport("{broken", 1000U, state));
  TEST_ASSERT_TRUE(state.connected);
  TEST_ASSERT_EQUAL(BambuPrintState::RUNNING, state.printState);
  TEST_ASSERT_EQUAL_STRING("RUNNING", state.gcodeState.c_str());
  TEST_ASSERT_TRUE(state.printing);
  TEST_ASSERT_EQUAL_UINT8(77U, state.progress);
  TEST_ASSERT_EQUAL_UINT16(15U, state.remainingMinutes);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 225.0f, state.nozzleTemp);
  TEST_ASSERT_EQUAL_STRING("keep", state.jobName.c_str());
  TEST_ASSERT_EQUAL_UINT32(900U, state.lastUpdateMs);
}

void test_invalid_ranges_and_oversized_strings_are_ignored() {
  BambuState state;
  state.progress = 50U;
  state.remainingMinutes = 20U;
  state.nozzleTemp = 210.0f;
  state.bedTemp = 60.0f;
  state.chamberTemp = 35.0f;
  state.layerNum = 10U;
  state.totalLayers = 100U;
  state.jobName = "safe-job";

  std::string longName(BambuStateLimits::JOB_NAME + 1U, 'x');
  const std::string payload = std::string("{\"print\":{\"mc_percent\":150,\"mc_remaining_time\":-1,\"nozzle_temper\":9999,\"bed_temper\":9999,\"chamber_temper\":9999,\"layer_num\":-4,\"total_layer_num\":-1,\"subtask_name\":\"") + longName + "\"}}";

  TEST_ASSERT_TRUE(applyBambuReport(payload, 700U, state));
  TEST_ASSERT_EQUAL_UINT8(50U, state.progress);
  TEST_ASSERT_EQUAL_UINT16(20U, state.remainingMinutes);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 210.0f, state.nozzleTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 60.0f, state.bedTemp);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 35.0f, state.chamberTemp);
  TEST_ASSERT_EQUAL_UINT16(10U, state.layerNum);
  TEST_ASSERT_EQUAL_UINT16(100U, state.totalLayers);
  TEST_ASSERT_EQUAL_STRING("safe-job", state.jobName.c_str());
  TEST_ASSERT_EQUAL_UINT32(700U, state.lastUpdateMs);
}

void test_basic_ams_tray_now_selects_active_filament() {
  BambuState state;
  const char* payload = R"({"print":{"ams":{"tray_now":"1","ams":[{"id":"0","tray":[{"id":"0","tray_type":"PLA","tray_sub_brands":"PLA Basic","tray_color":"FF0000FF","remain":80},{"id":"1","tray_type":"PETG HF","tray_color":"00FF00FF","remain":55}]}]}}})";

  TEST_ASSERT_TRUE(applyBambuReport(payload, 800U, state));
  TEST_ASSERT_TRUE(state.filament.present);
  TEST_ASSERT_FALSE(state.filament.externalSpool);
  TEST_ASSERT_EQUAL_INT16(1, state.filament.slot);
  TEST_ASSERT_EQUAL_STRING("PETG HF", state.filament.type.c_str());
  TEST_ASSERT_EQUAL_STRING("00FF00FF", state.filament.color.c_str());
  TEST_ASSERT_EQUAL_INT8(55, state.filament.remainingPercent);
  TEST_ASSERT_EQUAL_UINT32(800U, state.lastUpdateMs);
}

void test_unknown_state_is_preserved_as_other_without_crashing() {
  BambuState state;
  TEST_ASSERT_TRUE(applyBambuReport(R"({"print":{"gcode_state":"NEW_FIRMWARE_STATE"}})", 900U, state));
  TEST_ASSERT_EQUAL(BambuPrintState::OTHER, state.printState);
  TEST_ASSERT_EQUAL_STRING("NEW_FIRMWARE_STATE", state.gcodeState.c_str());
  TEST_ASSERT_FALSE(state.printing);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_running_report_populates_core_print_state);
  RUN_TEST(test_pause_and_finish_states_are_normalized);
  RUN_TEST(test_partial_delta_preserves_absent_fields);
  RUN_TEST(test_malformed_json_fails_without_mutating_state);
  RUN_TEST(test_invalid_ranges_and_oversized_strings_are_ignored);
  RUN_TEST(test_basic_ams_tray_now_selects_active_filament);
  RUN_TEST(test_unknown_state_is_preserved_as_other_without_crashing);
  return UNITY_END();
}
