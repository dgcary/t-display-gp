#include "BambuConfig.h"

#include <ArduinoJson.h>

namespace {
constexpr int CONFIG_SCHEMA = 1;

bool emailLooksValid(const std::string& email) {
  const size_t at = email.find('@');
  if (at == std::string::npos || at == 0 || at + 1 >= email.size()) return false;
  if (email.find_first_of(" \t\r\n") != std::string::npos) return false;
  return email.find('.', at + 1) != std::string::npos;
}

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
}  // namespace

BambuConfigValidationResult validateBambuConfig(const BambuConfig& config) {
  if (config.email.size() > BambuConfigLimits::EMAIL) return {BambuConfigError::EMAIL_TOO_LONG};
  if (config.password.size() > BambuConfigLimits::PASSWORD) return {BambuConfigError::PASSWORD_TOO_LONG};
  if (config.accessToken.size() > BambuConfigLimits::ACCESS_TOKEN) return {BambuConfigError::TOKEN_TOO_LONG};
  if (config.cloudUserId.size() > BambuConfigLimits::CLOUD_USER_ID) return {BambuConfigError::USER_ID_TOO_LONG};
  if (config.printerSerial.size() > BambuConfigLimits::PRINTER_SERIAL) return {BambuConfigError::SERIAL_TOO_LONG};
  if (config.printerName.size() > BambuConfigLimits::PRINTER_NAME) return {BambuConfigError::NAME_TOO_LONG};

  if (!config.email.empty() && !emailLooksValid(config.email)) return {BambuConfigError::EMAIL_INVALID};
  if (!config.enabled) return {};
  if (config.email.empty()) return {BambuConfigError::EMAIL_REQUIRED};
  if (config.password.empty() && config.accessToken.empty()) return {BambuConfigError::CREDENTIAL_REQUIRED};
  return {};
}

const char* bambuBrokerForRegion(BambuRegion region) {
  return region == BambuRegion::CHINA ? "cn.mqtt.bambulab.com" : "us.mqtt.bambulab.com";
}

bool BambuConfigCodec::encode(const BambuConfig& config, std::string& out) {
  if (!validateBambuConfig(config).ok()) return false;

  DynamicJsonDocument doc(6144);
  doc["schema"] = CONFIG_SCHEMA;
  doc["enabled"] = config.enabled;
  doc["region"] = regionName(config.region);
  doc["email"] = config.email;
  doc["password"] = config.password;
  doc["access_token"] = config.accessToken;
  doc["cloud_user_id"] = config.cloudUserId;
  doc["printer_serial"] = config.printerSerial;
  doc["printer_name"] = config.printerName;

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
  if (root.isNull()) return false;
  if (!root["schema"].is<int>() || root["schema"].as<int>() != CONFIG_SCHEMA) return false;
  if (!root["enabled"].is<bool>() || !root["region"].is<const char*>()) return false;

  BambuConfig decoded;
  decoded.enabled = root["enabled"].as<bool>();
  if (!parseRegion(root["region"].as<const char*>(), decoded.region)) return false;
  if (!readBoundedString(root["email"], BambuConfigLimits::EMAIL, decoded.email)) return false;
  if (!readBoundedString(root["password"], BambuConfigLimits::PASSWORD, decoded.password)) return false;
  if (!readBoundedString(root["access_token"], BambuConfigLimits::ACCESS_TOKEN, decoded.accessToken)) return false;
  if (!readBoundedString(root["cloud_user_id"], BambuConfigLimits::CLOUD_USER_ID, decoded.cloudUserId)) return false;
  if (!readBoundedString(root["printer_serial"], BambuConfigLimits::PRINTER_SERIAL, decoded.printerSerial)) return false;
  if (!readBoundedString(root["printer_name"], BambuConfigLimits::PRINTER_NAME, decoded.printerName)) return false;
  if (!validateBambuConfig(decoded).ok()) return false;

  out = std::move(decoded);
  return true;
}
