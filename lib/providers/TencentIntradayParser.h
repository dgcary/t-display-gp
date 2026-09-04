#pragma once

#include <string_view>

#include "IQuoteProvider.h"

namespace TencentIntradayParser {
ProviderError parse(std::string_view body, const StockSymbol& symbol, IntradaySeries& out);
}
