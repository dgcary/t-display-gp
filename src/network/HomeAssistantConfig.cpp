#include "HomeAssistantConfig.h"

#include <ArduinoJson.h>

#include <cctype>
#include <utility>

namespace {
constexpr size_t MAX_BASE_URL_BYTES = 160;
constexpr size_t MAX_TOKEN_BYTES = 512;
constexpr size_t MAX_CA_CERT_BYTES = 3072;
constexpr size_t MAX_ENTITY_ID_BYTES = 80;
constexpr size_t MAX_LABEL_BYTES = 30;
constexpr uint32_t CONFIG_SCHEMA = 1;

bool startsWith(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool isHttpUrl(const std::string& value) {
  return startsWith(value, "http://");
}

bool isHttpsUrl(const std::string& value) {
  return startsWith(value, "https://");
}

bool isCanonicalBaseUrl(const std::string& value) {
  if (value.empty() || value.size() > MAX_BASE_URL_BYTES) return false;
  if (!isHttpUrl(value) && !isHttpsUrl(value)) return false;
  if (value.back() == '/' || value.find('?') != std::string::npos ||
      value.find('#') != std::string::npos) {
    return false;
  }
  const size_t schemeEnd = value.find("://");
  const size_t authorityStart = schemeEnd == std::string::npos ? 0U : schemeEnd + 3U;
  return authorityStart < value.size();
}

bool validPemCertificate(const std::string& value) {
  return value.find("-----BEGIN CERTIFICATE-----") != std::string::npos &&
         value.find("-----END CERTIFICATE-----") != std::string::npos;
}

bool validEntityId(const std::string& value) {
  if (value.empty() || value.size() > MAX_ENTITY_ID_BYTES) return false;
  bool sawDot = false;
  for (unsigned char c : value) {
    if (c == '.') {
      if (sawDot) return false;
      sawDot = true;
      continue;
    }
    if (!(std::islower(c) || std::isdigit(c) || c == '_')) return false;
  }
  return sawDot && value.front() != '.' && value.back() != '.';
}
}  // namespace

const char* HomeAssistantConfigValidationResult::message() const {
  switch (error) {
    case HomeAssistantConfigError::NONE: return "ok";
    case HomeAssistantConfigError::REFRESH_INTERVAL: return "refresh must be 30 to 300 seconds";
    case HomeAssistantConfigError::BASE_URL: return "Home Assistant URL must be canonical HTTP/HTTPS without trailing slash";
    case HomeAssistantConfigError::TOKEN: return "Home Assistant token is missing or too long";
    case HomeAssistantConfigError::CA_CERT: return "HTTPS Home Assistant requires a valid CA certificate";
    case HomeAssistantConfigError::ENTITY_COUNT: return "configure 1 to 4 Home Assistant entities";
    case HomeAssistantConfigError::ENTITY_ID: return "invalid Home Assistant entity id";
    case HomeAssistantConfigError::LABEL: return "Home Assistant label is too long";
  }
  return "invalid Home Assistant configuration";
}

HomeAssistantConfigValidationResult validateHomeAssistantConfig(const HomeAssistantConfig& config) {
  if (config.refreshSeconds < 30 || config.refreshSeconds > 300) {
    return {HomeAssistantConfigError::REFRESH_INTERVAL, 0};
  }
  if (config.entityCount > config.entities.size()) {
    return {HomeAssistantConfigError::ENTITY_COUNT, 0};
  }
  if (!config.baseUrl.empty() && !isCanonicalBaseUrl(config.baseUrl)) {
    return {HomeAssistantConfigError::BASE_URL, 0};
  }
  if (config.token.size() > MAX_TOKEN_BYTES) return {HomeAssistantConfigError::TOKEN, 0};
  if (config.caCert.size() > MAX_CA_CERT_BYTES) return {HomeAssistantConfigError::CA_CERT, 0};
  for (size_t i = 0; i < config.entityCount; ++i) {
    if (!validEntityId(config.entities[i].entityId)) return {HomeAssistantConfigError::ENTITY_ID, i};
    if (config.entities[i].label.size() > MAX_LABEL_BYTES) return {HomeAssistantConfigError::LABEL, i};
  }

  if (!config.enabled) return {};
  if (config.baseUrl.empty()) return {HomeAssistantConfigError::BASE_URL, 0};
  if (config.token.empty()) return {HomeAssistantConfigError::TOKEN, 0};
  if (config.entityCount < 1) return {HomeAssistantConfigError::ENTITY_COUNT, 0};
  if (isHttpsUrl(config.baseUrl) && !validPemCertificate(config.caCert)) {
    return {HomeAssistantConfigError::CA_CERT, 0};
  }
  return {};
}

namespace HomeAssistantConfigCodec {
bool encode(const HomeAssistantConfig& config, std::string& out) {
  if (!validateHomeAssistantConfig(config).ok()) return false;
  DynamicJsonDocument doc(8192);
  doc["schema"] = CONFIG_SCHEMA;
  doc["enabled"] = config.enabled;
  doc["base_url"] = config.baseUrl;
  doc["token"] = config.token;
  doc["ca_cert"] = config.caCert;
  doc["refresh_sec"] = config.refreshSeconds;
  JsonArray entities = doc.createNestedArray("entities");
  for (size_t i = 0; i < config.entityCount; ++i) {
    JsonObject entity = entities.createNestedObject();
    entity["entity_id"] = config.entities[i].entityId;
    entity["label"] = config.entities[i].label;
  }
  std::string encoded;
  encoded.reserve(512 + config.caCert.size() + config.token.size());
  serializeJson(doc, encoded);
  if (encoded.size() > 6144) return false;
  out = std::move(encoded);
  return true;
}

bool decode(std::string_view json, HomeAssistantConfig& out) {
  if (json.empty() || json.size() > 6144) return false;
  DynamicJsonDocument doc(8192);
  const DeserializationError parseError = deserializeJson(doc, json.data(), json.size());
  if (parseError) return false;
  if (doc["schema"].as<uint32_t>() != CONFIG_SCHEMA) return false;

  HomeAssistantConfig parsed;
  parsed.enabled = doc["enabled"] | false;
  parsed.baseUrl = doc["base_url"] | "";
  parsed.token = doc["token"] | "";
  parsed.caCert = doc["ca_cert"] | "";
  parsed.refreshSeconds = doc["refresh_sec"] | 30U;
  JsonArrayConst entities = doc["entities"].as<JsonArrayConst>();
  if (entities.size() > parsed.entities.size()) return false;
  parsed.entityCount = entities.size();
  size_t index = 0;
  for (JsonObjectConst entity : entities) {
    parsed.entities[index].entityId = entity["entity_id"] | "";
    parsed.entities[index].label = entity["label"] | "";
    ++index;
  }
  if (!validateHomeAssistantConfig(parsed).ok()) return false;
  out = std::move(parsed);
  return true;
}
}  // namespace HomeAssistantConfigCodec
