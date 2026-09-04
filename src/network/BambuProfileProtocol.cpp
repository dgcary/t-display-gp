#include "BambuCloudProtocol.h"

#include <ArduinoJson.h>

#include <string>
#include <utility>

#include "BambuConfig.h"

namespace {
constexpr size_t PROFILE_BODY_MAX = 4096U;

bool readProfileUid(JsonObjectConst object, std::string& uid) {
  const char* stringFields[] = {"uidStr", "uid"};
  for (const char* field : stringFields) {
    JsonVariantConst value = object[field];
    if (!value.is<const char*>()) continue;
    const char* text = value.as<const char*>();
    if (!text || !*text) return false;
    const size_t length = std::char_traits<char>::length(text);
    if (length > BambuConfigLimits::CLOUD_USER_ID) return false;
    uid.assign(text, length);
    return true;
  }

  JsonVariantConst numeric = object["uid"];
  if (numeric.is<long long>()) {
    const long long value = numeric.as<long long>();
    if (value < 0) return false;
    uid = std::to_string(value);
    return true;
  }
  if (numeric.is<unsigned long long>()) {
    uid = std::to_string(numeric.as<unsigned long long>());
    return true;
  }
  return false;
}
}  // namespace

bool parseBambuProfileUserId(const std::string& body, std::string& userId) {
  if (body.empty() || body.size() > PROFILE_BODY_MAX) return false;

  DynamicJsonDocument doc(6144);
  if (deserializeJson(doc, body)) return false;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) return false;

  std::string uid;
  if (!readProfileUid(root, uid)) {
    JsonVariantConst data = root["data"];
    if (!data.is<JsonObjectConst>() || !readProfileUid(data.as<JsonObjectConst>(), uid)) {
      return false;
    }
  }

  if (uid.rfind("u_", 0) != 0) uid.insert(0, "u_");
  if (uid.size() > BambuConfigLimits::CLOUD_USER_ID) return false;

  userId = std::move(uid);
  return true;
}
