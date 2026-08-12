#pragma once

#include <string_view>

#include "IQuoteProvider.h"

namespace EastMoneyParser {
ProviderError parseQuote(std::string_view body, const StockSymbol& symbol, QuoteSnapshot& out);
ProviderError parseIntraday(std::string_view body, const StockSymbol& symbol, IntradaySeries& out);
}  // namespace EastMoneyParser
