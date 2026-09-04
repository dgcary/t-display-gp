#include "ProvisioningForm.h"

#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace {
bool isBlank(const std::string& value) {
  for (const unsigned char c : value) {
    if (!std::isspace(c)) return false;
  }
  return true;
}

bool parseUnsignedRange(const std::string& text, unsigned long minValue, unsigned long maxValue,
                        unsigned long& out) {
  char* end = nullptr;
  const unsigned long value = std::strtoul(text.c_str(), &end, 10);
  if (text.empty() || end == text.c_str() || *end != '\0' || value < minValue || value > maxValue) {
    return false;
  }
  out = value;
  return true;
}

bool parseEnabled(const std::string& value, bool& enabled) {
  if (value == "1" || value == "true" || value == "on") {
    enabled = true;
    return true;
  }
  if (value.empty() || value == "0" || value == "false" || value == "off") {
    enabled = false;
    return true;
  }
  return false;
}

bool parseCoordinate(const std::string& text, double minValue, double maxValue, int32_t& e6) {
  char* end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (text.empty() || end == text.c_str() || *end != '\0' || !std::isfinite(value) ||
      value < minValue || value > maxValue) {
    return false;
  }
  e6 = static_cast<int32_t>(std::llround(value * 1000000.0));
  return true;
}

std::string formatCoordinate(int32_t e6) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "%.6f", static_cast<double>(e6) / 1000000.0);
  return buffer;
}
}

bool ProvisioningForm::buildConfig(const ProvisioningFields& fields, AppConfig& out, std::string& error) {
  AppConfig candidate = AppConfig::defaults();

  unsigned long refresh = 0;
  if (!parseUnsignedRange(fields.refresh, 3, 5, refresh)) {
    error = "刷新间隔只能是 3、4 或 5 秒";
    return false;
  }
  candidate.quoteRefreshSec = static_cast<uint32_t>(refresh);

  for (size_t i = 0; i < fields.symbols.size(); ++i) {
    const bool symbolBlank = isBlank(fields.symbols[i]);
    const bool nameBlank = isBlank(fields.names[i]);
    if (symbolBlank && nameBlank) continue;
    if (symbolBlank) {
      error = "第 " + std::to_string(i + 1) + " 行有名称但没有股票代码";
      return false;
    }
    candidate.stocks.push_back({StockSymbol::parse(fields.symbols[i]), nameBlank ? "" : fields.names[i]});
  }

  bool weatherEnabled = false;
  if (!parseEnabled(fields.weatherEnabled, weatherEnabled)) {
    error = "天气启用状态无效";
    return false;
  }
  candidate.weather.enabled = weatherEnabled;

  unsigned long weatherRefresh = 0;
  if (!parseUnsignedRange(fields.weatherRefresh, 5, 60, weatherRefresh)) {
    error = "天气刷新间隔必须是 5 到 60 分钟";
    return false;
  }
  candidate.weather.refreshMinutes = static_cast<uint32_t>(weatherRefresh);

  const bool anyLocationField = !isBlank(fields.locationName) || !isBlank(fields.latitude) ||
                                !isBlank(fields.longitude);
  if (weatherEnabled || anyLocationField) {
    if (isBlank(fields.locationName) || isBlank(fields.latitude) || isBlank(fields.longitude)) {
      error = "启用天气时请填写地点名称、纬度和经度";
      return false;
    }
    int32_t latitudeE6 = 0;
    int32_t longitudeE6 = 0;
    if (!parseCoordinate(fields.latitude, -90.0, 90.0, latitudeE6)) {
      error = "纬度必须在 -90 到 90 之间";
      return false;
    }
    if (!parseCoordinate(fields.longitude, -180.0, 180.0, longitudeE6)) {
      error = "经度必须在 -180 到 180 之间";
      return false;
    }
    candidate.location.displayName = fields.locationName;
    candidate.location.latitudeE6 = latitudeE6;
    candidate.location.longitudeE6 = longitudeE6;
  }

  const ConfigValidationResult result = validate(candidate);
  if (!result.ok()) {
    switch (result.error) {
      case ConfigValidationError::STOCK_COUNT:
        error = "请配置 3 到 5 只股票";
        break;
      case ConfigValidationError::INVALID_SYMBOL:
        error = "第 " + std::to_string(result.stockIndex + 1) + " 只股票代码无效";
        break;
      case ConfigValidationError::DUPLICATE_SYMBOL:
        error = "股票代码不能重复";
        break;
      case ConfigValidationError::NAME_TOO_LONG:
        error = "股票显示名称过长";
        break;
      case ConfigValidationError::REFRESH_INTERVAL:
        error = "刷新间隔只能是 3、4 或 5 秒";
        break;
      case ConfigValidationError::LOCATION_NAME:
        error = "天气地点名称无效或过长";
        break;
      case ConfigValidationError::LOCATION_COORDINATES:
        error = "天气经纬度超出范围";
        break;
      case ConfigValidationError::WEATHER_REFRESH_INTERVAL:
        error = "天气刷新间隔必须是 5 到 60 分钟";
        break;
      case ConfigValidationError::SCHEMA_VERSION:
        error = "配置版本不兼容";
        break;
      case ConfigValidationError::NONE:
        break;
    }
    return false;
  }

  out = std::move(candidate);
  error.clear();
  return true;
}

ProvisioningFields ProvisioningForm::fromConfig(const AppConfig& config) {
  ProvisioningFields fields;
  fields.refresh = std::to_string(config.quoteRefreshSec);
  const size_t count = config.stocks.size() < fields.symbols.size() ? config.stocks.size() : fields.symbols.size();
  for (size_t i = 0; i < count; ++i) {
    fields.symbols[i] = config.stocks[i].symbol.canonical();
    fields.names[i] = config.stocks[i].displayName;
  }

  fields.locationName = config.location.displayName;
  if (!config.location.displayName.empty() || config.weather.enabled ||
      config.location.latitudeE6 != 0 || config.location.longitudeE6 != 0) {
    fields.latitude = formatCoordinate(config.location.latitudeE6);
    fields.longitude = formatCoordinate(config.location.longitudeE6);
  }
  fields.weatherEnabled = config.weather.enabled ? "1" : "0";
  fields.weatherRefresh = std::to_string(config.weather.refreshMinutes);
  return fields;
}
