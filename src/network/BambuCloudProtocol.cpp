#include "BambuCloudProtocol.h"

#include <ArduinoJson.h>

#include <cctype>
#include <cstdint>
#include <utility>

#include "BambuConfig.h"

namespace {
constexpr size_t LOGIN_BODY_MAX = 8192;
constexpr size_t DEVICE_BODY_MAX = 16384;
constexpr size_t PROFILE_BODY_MAX = 4096;
constexpr size_t JWT_TOKEN_MAX = BambuConfigLimits::ACCESS_TOKEN;
constexpr size_t JWT_PAYLOAD_MAX = 2048;
constexpr size_t TFA_KEY_MAX = 512;
constexpr size_t ERROR_TEXT_MAX = 256;
constexpr size_t DEVICE_MODEL_MAX = 64;

bool readBoundedString(JsonVariantConst value, size_t maxLen, std::string& out) {
  if (!value.is<const char*>()) return false;
  const char* text = value.as<const char*>();
  if (!text) return false;
  const size_t len = std::char_traits<char>::length(text);
  if (len == 0 || len > maxLen) return false;
  out.assign(text, len);
  return true;
}

bool readOptionalBoundedString(JsonVariantConst value, size_t maxLen, std::string& out) {
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

bool copyErrorText(JsonVariantConst value, std::string& out) {
  if (!value.is<const char*>()) return false;
  const char* text = value.as<const char*>();
  if (!text || !*text) return false;
  const size_t len = std::char_traits<char>::length(text);
  out.assign(text, len > ERROR_TEXT_MAX ? ERROR_TEXT_MAX : len);
  return true;
}

bool findErrorText(JsonObjectConst root, std::string& out) {
  if (copyErrorText(root["error"], out)) return true;
  if (copyErrorText(root["message"], out)) return true;

  JsonVariantConst data = root["data"];
  if (data.is<JsonObjectConst>()) {
    JsonObjectConst nested = data.as<JsonObjectConst>();
    if (copyErrorText(nested["error"], out)) return true;
    if (copyErrorText(nested["message"], out)) return true;
  }
  return false;
}

bool findToken(JsonObjectConst root, std::string& token) {
  if (readBoundedString(root["accessToken"], BambuConfigLimits::ACCESS_TOKEN, token)) return true;
  if (readBoundedString(root["token"], BambuConfigLimits::ACCESS_TOKEN, token)) return true;

  JsonVariantConst data = root["data"];
  if (!data.is<JsonObjectConst>()) return false;
  JsonObjectConst nested = data.as<JsonObjectConst>();
  if (readBoundedString(nested["accessToken"], BambuConfigLimits::ACCESS_TOKEN, token)) return true;
  return readBoundedString(nested["token"], BambuConfigLimits::ACCESS_TOKEN, token);
}

int decodeBase64UrlChar(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

bool decodeBase64Url(const std::string& encoded, std::string& decoded) {
  if (encoded.empty() || encoded.size() > JWT_PAYLOAD_MAX * 2 || encoded.size() % 4 == 1) return false;

  std::string candidate;
  candidate.reserve((encoded.size() * 3) / 4 + 2);

  uint32_t buffer = 0;
  unsigned bits = 0;
  for (char c : encoded) {
    if (c == '=') return false;
    const int value = decodeBase64UrlChar(c);
    if (value < 0) return false;

    buffer = (buffer << 6) | static_cast<uint32_t>(value);
    bits += 6;
    while (bits >= 8) {
      bits -= 8;
      candidate.push_back(static_cast<char>((buffer >> bits) & 0xFFu));
      if (candidate.size() > JWT_PAYLOAD_MAX) return false;
    }
    if (bits == 0) {
      buffer = 0;
    } else {
      buffer &= (1u << bits) - 1u;
    }
  }

  if (bits > 0 && buffer != 0) return false;
  decoded = std::move(candidate);
  return true;
}

bool readJwtUid(JsonObjectConst root, std::string& uid) {
  const char* fields[] = {"uid", "sub", "user_id"};
  for (const char* field : fields) {
    JsonVariantConst value = root[field];
    if (value.is<const char*>()) {
      if (readBoundedString(value, BambuConfigLimits::CLOUD_USER_ID, uid)) return true;
      return false;
    }
    if (value.is<long long>()) {
      uid = std::to_string(value.as<long long>());
      return !uid.empty();
    }
    if (value.is<unsigned long long>()) {
      uid = std::to_string(value.as<unsigned long long>());
      return !uid.empty();
    }
  }
  return false;
}

bool normalizeUserId(std::string& uid) {
  if (uid.empty()) return false;
  if (uid.rfind("u_", 0) != 0) uid.insert(0, "u_");
  return uid.size() <= BambuConfigLimits::CLOUD_USER_ID;
}

bool isSafeSerial(const std::string& serial) {
  if (serial.empty() || serial.size() > BambuConfigLimits::PRINTER_SERIAL) return false;
  for (unsigned char c : serial) {
    if (!(std::isalnum(c) || c == '_' || c == '-')) return false;
  }
  return true;
}

bool containsSerial(const std::vector<BambuCloudDevice>& devices, const std::string& serial) {
  for (const auto& device : devices) {
    if (device.serial == serial) return true;
  }
  return false;
}
}  // namespace

bool parseBambuLoginReply(int httpStatus, const std::string& body, BambuLoginReply& out) {
  if (body.empty() || body.size() > LOGIN_BODY_MAX) return false;

  DynamicJsonDocument doc(12288);
  if (deserializeJson(doc, body)) return false;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) return false;

  BambuLoginReply candidate;

  if (httpStatus != 200) {
    candidate.disposition = BambuLoginDisposition::ERROR;
    if (!findErrorText(root, candidate.error)) {
      candidate.error = "HTTP " + std::to_string(httpStatus);
    }
    out = std::move(candidate);
    return true;
  }

  if (findToken(root, candidate.accessToken)) {
    candidate.disposition = BambuLoginDisposition::TOKEN;
    out = std::move(candidate);
    return true;
  }

  std::string loginType;
  JsonVariantConst loginTypeValue = root["loginType"];
  if (!loginTypeValue.isNull()) {
    if (!loginTypeValue.is<const char*>()) return false;
    const char* text = loginTypeValue.as<const char*>();
    if (!text) return false;
    loginType = text;
  }

  if (loginType == "verifyCode") {
    candidate.disposition = BambuLoginDisposition::NEED_EMAIL_CODE;
    out = std::move(candidate);
    return true;
  }

  std::string tfaKey;
  const bool hasTfaKey = readBoundedString(root["tfaKey"], TFA_KEY_MAX, tfaKey);
  if (loginType == "tfa" || (loginType.empty() && hasTfaKey)) {
    if (!hasTfaKey) return false;
    candidate.disposition = BambuLoginDisposition::NEED_TFA;
    candidate.tfaKey = std::move(tfaKey);
    out = std::move(candidate);
    return true;
  }

  return false;
}

bool extractBambuUserIdFromJwt(const std::string& token, std::string& userId) {
  if (token.empty() || token.size() > JWT_TOKEN_MAX) return false;

  const size_t firstDot = token.find('.');
  if (firstDot == std::string::npos || firstDot == 0) return false;
  const size_t secondDot = token.find('.', firstDot + 1);
  if (secondDot == std::string::npos || secondDot == firstDot + 1 || secondDot + 1 >= token.size()) return false;
  if (token.find('.', secondDot + 1) != std::string::npos) return false;

  std::string payload;
  if (!decodeBase64Url(token.substr(firstDot + 1, secondDot - firstDot - 1), payload)) return false;

  DynamicJsonDocument doc(3072);
  if (deserializeJson(doc, payload)) return false;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) return false;

  std::string uid;
  if (!readJwtUid(root, uid) || !normalizeUserId(uid)) return false;

  userId = std::move(uid);
  return true;
}

bool parseBambuProfileUserId(const std::string& body, std::string& userId) {
  if (body.empty() || body.size() > PROFILE_BODY_MAX) return false;

  DynamicJsonDocument doc(6144);
  if (deserializeJson(doc, body)) return false;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) return false;

  std::string uid;
  if (root["uidStr"].is<const char*>()) {
    if (!readBoundedString(root["uidStr"], BambuConfigLimits::CLOUD_USER_ID, uid)) return false;
  } else if (root["uid"].is<const char*>()) {
    if (!readBoundedString(root["uid"], BambuConfigLimits::CLOUD_USER_ID, uid)) return false;
  } else if (root["uid"].is<long long>()) {
    const long long value = root["uid"].as<long long>();
    if (value < 0) return false;
    uid = std::to_string(value);
  } else if (root["uid"].is<unsigned long long>()) {
    uid = std::to_string(root["uid"].as<unsigned long long>());
  } else {
    return false;
  }

  if (!normalizeUserId(uid)) return false;
  userId = std::move(uid);
  return true;
}

bool parseBambuDeviceList(const std::string& body, std::vector<BambuCloudDevice>& out) {
  if (body.empty() || body.size() > DEVICE_BODY_MAX) return false;

  DynamicJsonDocument doc(24576);
  if (deserializeJson(doc, body)) return false;
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root.isNull()) return false;

  JsonArrayConst array;
  if (root["data"].is<JsonArrayConst>()) {
    array = root["data"].as<JsonArrayConst>();
  } else if (root["devices"].is<JsonArrayConst>()) {
    array = root["devices"].as<JsonArrayConst>();
  } else {
    return false;
  }

  std::vector<BambuCloudDevice> candidate;
  candidate.reserve(array.size());

  for (JsonVariantConst item : array) {
    if (!item.is<JsonObjectConst>()) return false;
    JsonObjectConst object = item.as<JsonObjectConst>();

    BambuCloudDevice device;
    if (!readBoundedString(object["dev_id"], BambuConfigLimits::PRINTER_SERIAL, device.serial)) return false;
    if (!isSafeSerial(device.serial) || containsSerial(candidate, device.serial)) return false;
    if (!readOptionalBoundedString(object["name"], BambuConfigLimits::PRINTER_NAME, device.name)) return false;
    if (!readOptionalBoundedString(object["dev_product_name"], DEVICE_MODEL_MAX, device.model)) return false;
    candidate.push_back(std::move(device));
  }

  out = std::move(candidate);
  return true;
}

std::string bambuReportTopic(const std::string& serial) {
  if (!isSafeSerial(serial)) return {};
  return "device/" + serial + "/report";
}
