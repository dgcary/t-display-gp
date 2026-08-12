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

struct AppConfig {
  uint32_t schemaVersion = 1;
  uint32_t quoteRefreshSec = 5;
  std::vector<ConfiguredStock> stocks;

  static AppConfig defaults();
};

enum class ConfigValidationError {
  NONE,
  SCHEMA_VERSION,
  STOCK_COUNT,
  INVALID_SYMBOL,
  DUPLICATE_SYMBOL,
  NAME_TOO_LONG,
  REFRESH_INTERVAL
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
bool decode(std::string_view json, AppConfig& out);
}  // namespace AppConfigCodec
