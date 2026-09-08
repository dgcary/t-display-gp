#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

enum class BambuPrintState {
  UNKNOWN = 0,
  IDLE,
  RUNNING,
  PAUSE,
  PREPARE,
  FINISH,
  FAILED,
  OTHER,
};

namespace BambuStateLimits {
constexpr size_t GCODE_STATE = 24U;
constexpr size_t JOB_NAME = 96U;
constexpr size_t FILAMENT_TYPE = 32U;
constexpr size_t FILAMENT_COLOR = 16U;
constexpr size_t REPORT_JSON = 65536U;
}  // namespace BambuStateLimits

struct BambuFilamentState {
  bool present = false;
  bool externalSpool = false;
  int16_t slot = -1;
  std::string type;
  std::string color;
  int8_t remainingPercent = -1;
};

struct BambuState {
  bool connected = false;
  bool printing = false;
  std::string gcodeState;
  BambuPrintState printState = BambuPrintState::UNKNOWN;
  uint8_t progress = 0U;
  uint16_t remainingMinutes = 0U;
  float nozzleTemp = 0.0f;
  float nozzleTarget = 0.0f;
  float bedTemp = 0.0f;
  float bedTarget = 0.0f;
  float chamberTemp = 0.0f;
  uint16_t layerNum = 0U;
  uint16_t totalLayers = 0U;
  std::string jobName;
  BambuFilamentState filament;
  uint32_t lastUpdateMs = 0U;
};

BambuPrintState normalizeBambuPrintState(std::string_view rawState);
bool applyBambuReport(std::string_view json, uint32_t nowMs, BambuState& state);
