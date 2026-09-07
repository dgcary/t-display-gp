#include "BambuConfig.h"

#include <ArduinoJson.h>

#include <cctype>
#include <utility>

namespace {
constexpr int CONFIG_SCHEMA_V1 = 1;
constexpr int CONFIG_SCHEMA_V2 = 2;

const char* regionName(BambuRegion region) {
  return region == BambuRegion::CHINA ? "china" : "us_eu";
}

bool parseRegion(const char* value, BambuRegion& out) {
  if (!value) return false;
  const std::string region(value);
  if (region == "us_eu") {
    out = BambuRegion::US_EU;
    return true;
  }
  if (region == "china") {
    out = BambuRegion::CHINA;
    return true;
  }
  return false;
}

bool readBoundedString(JsonVariantConst value, size_t maxLen, std::string& out) {
  if (value.isNull()) {
    out.clear();
    return true;
  }
  if (!value.is<const char*>()) return false;
  const char* text = value.as<const char*>();
  if (!text) {
    out.clear();
    return true;
  }
  const size_t len = std::char_traits<char>::length(text);
  if (len > maxLen) return false;
  out.assign(text, len);
  return true;
}

bool safeSerial(const std::string& serial) {
  if (serial.empty() || serial.size() > BambuConfigLimits::PRINTER_SERIAL) return false;
  for (unsigned char c : serial) {
    if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
  }
  return true;
}

bool decodeV1(JsonObjectConst root, BambuConfig& decoded) {
  if (!root["enabled"].is<bool>() || !root["region"].is<const char*>()) return false;
  decoded.enabled = root["enabled"].as<bool>();
  if (!parseRegion(root["region"].as<const char*>(), decoded.region)) return false;
  if (!readBoundedString(root["access_token"], BambuConfigLimits::ACCESS_TOKEN, decoded.accessToken)) return false;
  if (!readBoundedString(root["cloud_user_id"], BambuConfigLimits::CLOUD_USER_ID, decoded.cloudUserId)) return false;

  std::string serial;
  std::string name;
  if (!readBoundedString(root["printer_serial"], BambuConfigLimits::PRINTER_SERIAL, serial)) return false;
  if (!readBoundedString(root["printer_name"], BambuConfigLimits::PRINTER_NAME, name)) return false;
  if (!serial.empty()) {
    decoded.printerCount = 1;
    decoded.activePrinterIndex = 0;
    decoded.printers[0].serial = std::move(serial);
    decoded.printers[0].name = std::move(name);
  }

  if (decoded.enabled && !validateBambuConfig(decoded).ok()) {
    // Preserve any reusable token/user-id/printer values for the new portal,
    // but do not start MQTT from an incomplete legacy config.
    decoded.enabled = false;
  }
  return validateBambuConfig(decoded).ok();
}

bool decodeV2(JsonObjectConst root, BambuConfig& decoded) {
  if (!root["enabled"].is<bool>() || !root["region"].is<const char*>() ||
      !root["printers"].is<JsonArrayConst>() || !root["active_printer"].is<size_t>()) {
    return false;
  }
  decoded.enabled = root["enabled"].as<bool>();
  if (!parseRegion(root["region"].as<const char*>(), decoded.region)) return false;
  if (!readBoundedString(root["access_token"], BambuConfigLimits::ACCESS_TOKEN, decoded.accessToken)) return false;
  if (!readBoundedString(root["cloud_user_id"], BambuConfigLimits::CLOUD_USER_ID, decoded.cloudUserId)) return false;

  JsonArrayConst printers = root["printers"].as<JsonArrayConst>();
  if (printers.size() > BambuConfigLimits::PRINTER_COUNT) return false;
  for (JsonVariantConst item : printers) {
    if (!item.is<JsonObjectConst>()) return false;
    JsonObjectConst object = item.as<JsonObjectConst>();
    BambuPrinterConfig& printer = decoded.printers[decoded.printerCount];
    if (!readBoundedString(object["serial"], BambuConfigLimits::PRINTER_SERIAL, printer.serial)) return false;
    if (!readBoundedString(object["name"], BambuConfigLimits::PRINTER_NAME, printer.name)) return false;
    ++decoded.printerCount;
  }
  decoded.activePrinterIndex = root["active_printer"].as<size_t>();
  return validateBambuConfig(decoded).ok();
}
}  // namespace

BambuConfigValidationResult validateBambuConfig(const BambuConfig& config) {
  if (config.accessToken.size() > BambuConfigLimits::ACCESS_TOKEN) return {BambuConfigError::TOKEN_TOO_LONG};
  if (config.cloudUserId.size() > BambuConfigLimits::CLOUD_USER_ID) return {BambuConfigError::USER_ID_TOO_LONG};
  if (config.printerCount > BambuConfigLimits::PRINTER_COUNT) return {BambuConfigError::TOO_MANY_PRINTERS};

  for (size_t i = 0; i < config.printerCount; ++i) {
    const BambuPrinterConfig& printer = config.printers[i];
    if (printer.serial.empty()) return {BambuConfigError::SERIAL_REQUIRED};
    if (printer.serial.size() > BambuConfigLimits::PRINTER_SERIAL) return {BambuConfigError::SERIAL_TOO_LONG};
    if (!safeSerial(printer.serial)) return {BambuConfigError::SERIAL_INVALID};
    if (printer.name.size() > BambuConfigLimits::PRINTER_NAME) return {BambuConfigError::NAME_TOO_LONG};
    for (size_t earlier = 0; earlier < i; ++earlier) {
      if (config.printers[earlier].serial == printer.serial) return {BambuConfigError::DUPLICATE_SERIAL};
    }
  }

  if (config.printerCount == 0) {
    if (config.activePrinterIndex != 0) return {BambuConfigError::ACTIVE_PRINTER_INVALID};
  } else if (config.activePrinterIndex >= config.printerCount) {
    return {BambuConfigError::ACTIVE_PRINTER_INVALID};
  }

  if (!config.enabled) return {};
  if (config.accessToken.empty()) return {BambuConfigError::TOKEN_REQUIRED};
  if (config.cloudUserId.empty()) return {BambuConfigError::USER_ID_REQUIRED};
  if (config.printerCount == 0) return {BambuConfigError::PRINTER_REQUIRED};
  return {};
}

const BambuPrinterConfig* activeBambuPrinter(const BambuConfig& config) {
  if (config.printerCount == 0 || config.printerCount > BambuConfigLimits::PRINTER_COUNT ||
      config.activePrinterIndex >= config.printerCount) {
    return nullptr;
  }
  return &config.printers[config.activePrinterIndex];
}

const char* bambuBrokerForRegion(BambuRegion region) {
  return region == BambuRegion::CHINA ? "cn.mqtt.bambulab.com" : "us.mqtt.bambulab.com";
}

bool BambuConfigCodec::encode(const BambuConfig& config, std::string& out) {
  if (!validateBambuConfig(config).ok()) return false;

  DynamicJsonDocument doc(6144);
  doc["schema"] = CONFIG_SCHEMA_V2;
  doc["enabled"] = config.enabled;
  doc["region"] = regionName(config.region);
  doc["access_token"] = config.accessToken;
  doc["cloud_user_id"] = config.cloudUserId;
  doc["active_printer"] = config.activePrinterIndex;
  JsonArray printers = doc.createNestedArray("printers");
  for (size_t i = 0; i < config.printerCount; ++i) {
    JsonObject printer = printers.createNestedObject();
    printer["serial"] = config.printers[i].serial;
    printer["name"] = config.printers[i].name;
  }

  std::string encoded;
  serializeJson(doc, encoded);
  if (encoded.empty() || encoded.size() > BambuConfigLimits::ENCODED) return false;
  out = std::move(encoded);
  return true;
}

bool BambuConfigCodec::decode(const std::string& encoded, BambuConfig& out) {
  if (encoded.empty() || encoded.size() > BambuConfigLimits::ENCODED) return false;

  DynamicJsonDocument doc(6144);
  if (deserializeJson(doc, encoded)) return false;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull() || !root["schema"].is<int>()) return false;

  BambuConfig decoded;
  const int schema = root["schema"].as<int>();
  const bool ok = schema == CONFIG_SCHEMA_V1 ? decodeV1(root, decoded)
                                             : (schema == CONFIG_SCHEMA_V2 ? decodeV2(root, decoded) : false);
  if (!ok) return false;
  out = std::move(decoded);
  return true;
}
