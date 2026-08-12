#pragma once

#include <string_view>

#include "IQuoteProvider.h"

namespace TencentParser {
ProviderError parseQuote(std::string_view body, const StockSymbol& symbol, QuoteSnapshot& out);
}  // namespace TencentParser
