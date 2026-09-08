#include "BambuState.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>

namespace {
constexpr float NOZZLE_MIN_C = -20.0f;
constexpr float NOZZLE_MAX_C = 500.0f;
constexpr float BED_MIN_C = -20.0f;
constexpr float BED_MAX_C = 200.0f;
constexpr float CHAMBER_MIN_C = -20.0f;
constexpr float CHAMBER_MAX_C = 150.0f;

bool readNumber(JsonVariantConst value, float& out) {
  if (value.is<float>()) {
    out = value.as<float>();
    return std::isfinite(out);
  }
  if (value.is<double>()) {
    const double parsed = value.as<double>();
    if (!std::isfinite(parsed)) return false;
    out = static_cast<float>(parsed);
    return true;
  }
  if (value.is<int>()) {
    out = static_cast<float>(value.as<int>());
    return true;
  }
  if (value.is<unsigned int>()) {
    out = static_cast<float>(value.as<unsigned int>());
    return true;
  }
  return false;
}

bool readBoundedUnsigned(JsonVariantConst value, uint32_t maxValue, uint32_t& out) {
  if (value.is<int>()) {
    const int parsed = value.as<int>();
    if (parsed < 0 || static_cast<uint32_t>(parsed) > maxValue) return false;
    out = static_cast<uint32_t>(parsed);
    return true;
  }
  if (value.is<unsigned int>()) {
    const uint32_t parsed = value.as<unsigned int>();
    if (parsed > maxValue) return false;
    out = parsed;
    return true;
  }
  return false;
}

bool readBoundedString(JsonVariantConst value, size_t maxLen, std::string& out) {
  if (!value.is<const char*>()) return false;
  const char* text = value.as<const char*>();
  if (!text) return false;
  const size_t length = std::char_traits<char>::length(text);
  if (length > maxLen) return false;
  out.assign(text, length);
  return true;
}

bool readTemperature(JsonVariantConst value, float minValue, float maxValue, float& out) {
  float parsed = 0.0f;
  if (!readNumber(value, parsed) || parsed < minValue || parsed > maxValue) return false;
  out = parsed;
  return true;
}

bool parseSmallDecimal(const char* text, int& out) {
  if (!text || *text == '\0') return false;
  char* end = nullptr;
  const long value = std::strtol(text, &end, 10);
  if (end == text || *end != '\0' || value < 0 || value > 255) return false;
  out = static_cast<int>(value);
  return true;
}

bool readSmallDecimal(JsonVariantConst value, int& out) {
  if (value.is<int>()) {
    const int parsed = value.as<int>();
    if (parsed < 0 || parsed > 255) return false;
    out = parsed;
    return true;
  }
  if (value.is<unsigned int>()) {
    const unsigned int parsed = value.as<unsigned int>();
    if (parsed > 255U) return false;
    out = static_cast<int>(parsed);
    return true;
  }
  if (value.is<const char*>()) return parseSmallDecimal(value.as<const char*>(), out);
  return false;
}

bool isHexColor(std::string_view value) {
  if (value.size() != 6U && value.size() != 8U) return false;
  for (const char c : value) {
    const bool digit = c >= '0' && c <= '9';
    const bool lower = c >= 'a' && c <= 'f';
    const bool upper = c >= 'A' && c <= 'F';
    if (!digit && !lower && !upper) return false;
  }
  return true;
}

void clearFilament(BambuFilamentState& filament) {
  filament = BambuFilamentState{};
}

bool parseTray(JsonObjectConst tray, int16_t slot, bool externalSpool,
               BambuFilamentState& out) {
  if (tray.isNull()) return false;

  std::string type;
  if (tray["tray_sub_brands"].is<const char*>()) {
    const char* subBrand = tray["tray_sub_brands"].as<const char*>();
    if (subBrand && *subBrand != '\0') {
      if (!readBoundedString(tray["tray_sub_brands"], BambuStateLimits::FILAMENT_TYPE, type)) {
        return false;
      }
    }
  }
  if (type.empty() && tray["tray_type"].is<const char*>()) {
    if (!readBoundedString(tray["tray_type"], BambuStateLimits::FILAMENT_TYPE, type)) return false;
  }
  if (type.empty()) return false;

  BambuFilamentState parsed;
  parsed.present = true;
  parsed.externalSpool = externalSpool;
  parsed.slot = slot;
  parsed.type = std::move(type);

  std::string color;
  if (tray["tray_color"].is<const char*>() &&
      readBoundedString(tray["tray_color"], BambuStateLimits::FILAMENT_COLOR, color) &&
      isHexColor(color)) {
    parsed.color = std::move(color);
  } else if (tray["cols"].is<JsonArrayConst>()) {
    JsonArrayConst colors = tray["cols"].as<JsonArrayConst>();
    if (!colors.isNull() && colors.size() > 0U) {
      std::string fallback;
      if (readBoundedString(colors[0], BambuStateLimits::FILAMENT_COLOR, fallback) &&
          isHexColor(fallback)) {
        parsed.color = std::move(fallback);
      }
    }
  }

  uint32_t remaining = 0U;
  if (readBoundedUnsigned(tray["remain"], 100U, remaining)) {
    parsed.remainingPercent = static_cast<int8_t>(remaining);
  }

  out = std::move(parsed);
  return true;
}

bool parseActiveFilament(JsonObjectConst ams, BambuFilamentState& out) {
  if (ams.isNull() || ams["tray_now"].isNull()) return false;

  int trayNow = -1;
  if (!readSmallDecimal(ams["tray_now"], trayNow)) return false;

  if (trayNow == 255) {
    clearFilament(out);
    return true;
  }

  if (trayNow == 254) {
    BambuFilamentState parsed;
    if (ams["vt_tray"].is<JsonObjectConst>() &&
        parseTray(ams["vt_tray"].as<JsonObjectConst>(), 254, true, parsed)) {
      out = std::move(parsed);
    } else {
      parsed.present = true;
      parsed.externalSpool = true;
      parsed.slot = 254;
      out = std::move(parsed);
    }
    return true;
  }

  if (!ams["ams"].is<JsonArrayConst>()) return false;
  const int wantedUnit = trayNow / 4;
  const int wantedTray = trayNow % 4;

  for (JsonObjectConst unit : ams["ams"].as<JsonArrayConst>()) {
    int unitId = -1;
    if (!readSmallDecimal(unit["id"], unitId) || unitId != wantedUnit) continue;
    if (!unit["tray"].is<JsonArrayConst>()) continue;

    for (JsonObjectConst tray : unit["tray"].as<JsonArrayConst>()) {
      int trayId = -1;
      if (!readSmallDecimal(tray["id"], trayId) || trayId != wantedTray) continue;
      BambuFilamentState parsed;
      if (!parseTray(tray, static_cast<int16_t>(trayNow), false, parsed)) return false;
      out = std::move(parsed);
      return true;
    }
  }
  return false;
}

void configureFilter(DynamicJsonDocument& filter) {
  JsonObject pf = filter["print"].to<JsonObject>();
  pf["gcode_state"] = true;
  pf["mc_percent"] = true;
  pf["mc_remaining_time"] = true;
  pf["nozzle_temper"] = true;
  pf["nozzle_target_temper"] = true;
  pf["bed_temper"] = true;
  pf["bed_target_temper"] = true;
  pf["chamber_temper"] = true;
  pf["subtask_name"] = true;
  pf["layer_num"] = true;
  pf["total_layer_num"] = true;

  JsonObject af = pf["ams"].to<JsonObject>();
  af["tray_now"] = true;
  JsonObject vt = af["vt_tray"].to<JsonObject>();
  vt["tray_type"] = true;
  vt["tray_sub_brands"] = true;
  vt["tray_color"] = true;
  vt["cols"][0] = true;
  vt["remain"] = true;

  JsonObject unit = af["ams"][0].to<JsonObject>();
  unit["id"] = true;
  JsonObject tray = unit["tray"][0].to<JsonObject>();
  tray["id"] = true;
  tray["tray_type"] = true;
  tray["tray_sub_brands"] = true;
  tray["tray_color"] = true;
  tray["cols"][0] = true;
  tray["remain"] = true;
}
}  // namespace

BambuPrintState normalizeBambuPrintState(std::string_view rawState) {
  if (rawState.empty() || rawState == "UNKNOWN") return BambuPrintState::UNKNOWN;
  if (rawState == "IDLE") return BambuPrintState::IDLE;
  if (rawState == "RUNNING") return BambuPrintState::RUNNING;
  if (rawState == "PAUSE") return BambuPrintState::PAUSE;
  if (rawState == "PREPARE") return BambuPrintState::PREPARE;
  if (rawState == "FINISH") return BambuPrintState::FINISH;
  if (rawState == "FAILED") return BambuPrintState::FAILED;
  return BambuPrintState::OTHER;
}

bool applyBambuReport(std::string_view json, uint32_t nowMs, BambuState& state) {
  if (json.empty() || json.size() > BambuStateLimits::REPORT_JSON) return false;

  DynamicJsonDocument filter(2048);
  configureFilter(filter);
  DynamicJsonDocument doc(8192);
  const DeserializationError error = deserializeJson(
      doc, json.data(), json.size(), DeserializationOption::Filter(filter));
  if (error) return false;

  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) return false;

  BambuState next = state;
  JsonObjectConst print = root["print"].as<JsonObjectConst>();
  if (!print.isNull()) {
    std::string rawState;
    if (readBoundedString(print["gcode_state"], BambuStateLimits::GCODE_STATE, rawState) &&
        !rawState.empty()) {
      next.gcodeState = std::move(rawState);
      next.printState = normalizeBambuPrintState(next.gcodeState);
      next.printing = next.printState == BambuPrintState::RUNNING ||
                      next.printState == BambuPrintState::PAUSE ||
                      next.printState == BambuPrintState::PREPARE;
    }

    uint32_t unsignedValue = 0U;
    if (readBoundedUnsigned(print["mc_percent"], 100U, unsignedValue)) {
      next.progress = static_cast<uint8_t>(unsignedValue);
    }
    if (readBoundedUnsigned(print["mc_remaining_time"],
                            std::numeric_limits<uint16_t>::max(), unsignedValue)) {
      next.remainingMinutes = static_cast<uint16_t>(unsignedValue);
    }

    float temperature = 0.0f;
    if (readTemperature(print["nozzle_temper"], NOZZLE_MIN_C, NOZZLE_MAX_C, temperature)) {
      next.nozzleTemp = temperature;
    }
    if (readTemperature(print["nozzle_target_temper"], NOZZLE_MIN_C, NOZZLE_MAX_C, temperature)) {
      next.nozzleTarget = temperature;
    }
    if (readTemperature(print["bed_temper"], BED_MIN_C, BED_MAX_C, temperature)) {
      next.bedTemp = temperature;
    }
    if (readTemperature(print["bed_target_temper"], BED_MIN_C, BED_MAX_C, temperature)) {
      next.bedTarget = temperature;
    }
    if (readTemperature(print["chamber_temper"], CHAMBER_MIN_C, CHAMBER_MAX_C, temperature)) {
      next.chamberTemp = temperature;
    }

    if (readBoundedUnsigned(print["layer_num"], std::numeric_limits<uint16_t>::max(), unsignedValue)) {
      next.layerNum = static_cast<uint16_t>(unsignedValue);
    }
    if (readBoundedUnsigned(print["total_layer_num"],
                            std::numeric_limits<uint16_t>::max(), unsignedValue)) {
      next.totalLayers = static_cast<uint16_t>(unsignedValue);
    }

    std::string jobName;
    if (readBoundedString(print["subtask_name"], BambuStateLimits::JOB_NAME, jobName)) {
      next.jobName = std::move(jobName);
    }

    if (print["ams"].is<JsonObjectConst>()) {
      BambuFilamentState filament = next.filament;
      if (parseActiveFilament(print["ams"].as<JsonObjectConst>(), filament)) {
        next.filament = std::move(filament);
      }
    }
  }

  next.lastUpdateMs = nowMs;
  state = std::move(next);
  return true;
}
