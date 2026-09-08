#include "TencentIntradayParser.h"

#include <ArduinoJson.h>

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

namespace {

bool parseDouble(std::string_view token, double& out) {
  if (token.empty()) return false;
  std::string text(token);
  char* end = nullptr;
  errno = 0;
  const double value = std::strtod(text.c_str(), &end);
  if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(value)) return false;
  out = value;
  return true;
}

bool parseUint32(std::string_view token, uint32_t& out) {
  double value = 0;
  if (!parseDouble(token, value) || value < 0 || std::floor(value) != value ||
      value > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
    return false;
  }
  out = static_cast<uint32_t>(value);
  return true;
}

bool splitRow(std::string_view row, std::array<std::string_view, 4>& fields) {
  size_t pos = 0;
  for (size_t i = 0; i < fields.size(); ++i) {
    while (pos < row.size() && row[pos] == ' ') ++pos;
    if (pos >= row.size()) return false;
    const size_t begin = pos;
    while (pos < row.size() && row[pos] != ' ') ++pos;
    fields[i] = row.substr(begin, pos - begin);
  }
  while (pos < row.size() && row[pos] == ' ') ++pos;
  return pos == row.size();
}

bool parseMinute(std::string_view token, uint16_t& minuteOfDay) {
  if (token.size() != 4) return false;
  for (char c : token) {
    if (c < '0' || c > '9') return false;
  }
  const int hour = (token[0] - '0') * 10 + (token[1] - '0');
  const int minute = (token[2] - '0') * 10 + (token[3] - '0');
  if (hour > 23 || minute > 59) return false;
  minuteOfDay = static_cast<uint16_t>(hour * 60 + minute);
  return true;
}

bool inTradingSession(uint16_t minute) {
  return (minute >= 9 * 60 + 30 && minute <= 11 * 60 + 30) ||
         (minute >= 13 * 60 && minute <= 15 * 60);
}

}  // namespace

namespace TencentIntradayParser {

ProviderError parse(std::string_view body, const StockSymbol& symbol, IntradaySeries& out) {
  if (!symbol.valid()) return ProviderError::UNSUPPORTED;

  const std::string providerCode = symbol.tencentCode();
  DynamicJsonDocument filter(768);
  filter["code"] = true;
  filter["data"][providerCode]["data"]["data"] = true;

  DynamicJsonDocument document(24576);
  const DeserializationError jsonError =
      deserializeJson(document, body.data(), body.size(), DeserializationOption::Filter(filter));
  if (jsonError) return ProviderError::PARSE;

  if (!document["code"].is<int>() || document["code"].as<int>() != 0) return ProviderError::PARSE;

  const JsonArrayConst rows = document["data"][providerCode]["data"]["data"].as<JsonArrayConst>();
  if (rows.isNull()) return ProviderError::MISSING_FIELD;

  IntradaySeries parsed;
  parsed.reserve(rows.size() < 242 ? rows.size() : 242);
  uint16_t lastMinute = 0;
  bool haveLastMinute = false;

  for (JsonVariantConst item : rows) {
    if (!item.is<const char*>()) return ProviderError::PARSE;
    const char* raw = item.as<const char*>();
    if (!raw) return ProviderError::PARSE;

    std::array<std::string_view, 4> fields{};
    if (!splitRow(raw, fields)) return ProviderError::PARSE;

    uint16_t minute = 0;
    if (!parseMinute(fields[0], minute)) return ProviderError::PARSE;
    if (!inTradingSession(minute)) continue;
    if (haveLastMinute && minute <= lastMinute) return ProviderError::PARSE;

    double price = 0;
    uint32_t cumulativeLots = 0;
    double cumulativeAmount = 0;
    if (!parseDouble(fields[1], price) || price <= 0 ||
        !parseUint32(fields[2], cumulativeLots) ||
        !parseDouble(fields[3], cumulativeAmount) || cumulativeAmount < 0) {
      return ProviderError::PARSE;
    }

    double averagePrice = price;
    if (cumulativeLots > 0 && cumulativeAmount > 0) {
      averagePrice = cumulativeAmount / (static_cast<double>(cumulativeLots) * 100.0);
    }
    if (!std::isfinite(averagePrice) || averagePrice <= 0 ||
        price > static_cast<double>(std::numeric_limits<float>::max()) ||
        averagePrice > static_cast<double>(std::numeric_limits<float>::max())) {
      return ProviderError::PARSE;
    }

    IntradayPoint point;
    point.minuteOfDay = minute;
    point.price = static_cast<float>(price);
    point.averagePrice = static_cast<float>(averagePrice);
    point.volume = cumulativeLots;
    parsed.push_back(point);
    lastMinute = minute;
    haveLastMinute = true;
    if (parsed.size() >= 242) break;
  }

  if (parsed.empty()) return ProviderError::MISSING_FIELD;
  out = std::move(parsed);
  return ProviderError::NONE;
}

}  // namespace TencentIntradayParser
