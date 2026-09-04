#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "StockSymbol.h"

struct ConfiguredStock {
  StockSymbol symbol;
  std::string displayName;
};

struct LocationConfig {
  std::string displayName;
  int32_t latitudeE6 = 0;
  int32_t longitudeE6 = 0;
};

struct WeatherConfig {
  bool enabled = false;
  uint32_t refreshMinutes = 15;
};

struct AppConfig {
  uint32_t schemaVersion = 2;
  uint32_t quoteRefreshSec = 5;
  std::vector<ConfiguredStock> stocks;
  LocationConfig location;
  WeatherConfig weather;

  static AppConfig defaults();
};

enum class ConfigValidationError {
  NONE,
  SCHEMA_VERSION,
  STOCK_COUNT,
  INVALID_SYMBOL,
  DUPLICATE_SYMBOL,
  NAME_TOO_LONG,
  REFRESH_INTERVAL,
  LOCATION_NAME,
  LOCATION_COORDINATES,
  WEATHER_REFRESH_INTERVAL
};

struct ConfigValidationResult {
  ConfigValidationError error = ConfigValidationError::NONE;
  size_t stockIndex = 0;

  bool ok() const { return error == ConfigValidationError::NONE; }
  const char* message() const;
};

ConfigValidationResult validate(const AppConfig& config);

namespace AppConfigCodec {
bool encode(const AppConfig& config, std::string& out);
bool decode(std::string_view json, AppConfig& out, uint32_t* sourceSchemaVersion = nullptr);
}  // namespace AppConfigCodec
