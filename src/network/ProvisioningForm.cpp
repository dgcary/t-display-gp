#include "ProvisioningForm.h"

#include <cctype>
#include <cstdlib>
#include <utility>

namespace {
bool isBlank(const std::string& value) {
  for (const unsigned char c : value) {
    if (!std::isspace(c)) return false;
  }
  return true;
}
}

bool ProvisioningForm::buildConfig(const ProvisioningFields& fields, AppConfig& out, std::string& error) {
  AppConfig candidate = AppConfig::defaults();
  char* end = nullptr;
  const unsigned long refresh = std::strtoul(fields.refresh.c_str(), &end, 10);
  if (fields.refresh.empty() || end == fields.refresh.c_str() || *end != '\0' || refresh < 3 || refresh > 5) {
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
  return fields;
}
