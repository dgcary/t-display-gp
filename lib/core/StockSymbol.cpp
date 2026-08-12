#include "StockSymbol.h"

#include <algorithm>
#include <cctype>

namespace {

std::string trim(std::string_view value) {
  size_t begin = 0;
  size_t end = value.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  return std::string(value.substr(begin, end - begin));
}

bool isSixDigits(const std::string& value) {
  return value.size() == 6 &&
         std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isdigit(ch); });
}

Exchange inferExchange(const std::string& code) {
  if (code.rfind("60", 0) == 0 || code.rfind("68", 0) == 0) {
    return Exchange::SSE;
  }
  if (code.rfind("00", 0) == 0 || code.rfind("30", 0) == 0) {
    return Exchange::SZSE;
  }
  if (code.rfind("4", 0) == 0 || code.rfind("8", 0) == 0 || code.rfind("92", 0) == 0) {
    return Exchange::BSE;
  }
  return Exchange::UNKNOWN;
}

Exchange parseSuffix(const std::string& suffix) {
  if (suffix == "SH") return Exchange::SSE;
  if (suffix == "SZ") return Exchange::SZSE;
  if (suffix == "BJ") return Exchange::BSE;
  return Exchange::UNKNOWN;
}

const char* canonicalSuffix(Exchange exchange) {
  switch (exchange) {
    case Exchange::SSE:
      return "SH";
    case Exchange::SZSE:
      return "SZ";
    case Exchange::BSE:
      return "BJ";
    default:
      return "";
  }
}

}  // namespace

StockSymbol StockSymbol::parse(std::string_view raw) {
  std::string value = trim(raw);
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });

  std::string code = value;
  Exchange exchange = Exchange::UNKNOWN;
  const auto dot = value.find('.');
  if (dot != std::string::npos) {
    if (value.find('.', dot + 1) != std::string::npos) {
      return {};
    }
    code = value.substr(0, dot);
    exchange = parseSuffix(value.substr(dot + 1));
    if (exchange == Exchange::UNKNOWN) {
      return {};
    }
  }

  if (!isSixDigits(code)) {
    return {};
  }
  if (exchange == Exchange::UNKNOWN) {
    exchange = inferExchange(code);
  }
  if (exchange == Exchange::UNKNOWN) {
    return {};
  }
  return StockSymbol(code, exchange);
}

std::string StockSymbol::canonical() const {
  if (!valid_) return {};
  return code_ + "." + canonicalSuffix(exchange_);
}

std::string StockSymbol::eastMoneySecId() const {
  if (!valid_) return {};
  return std::string(exchange_ == Exchange::SSE ? "1." : "0.") + code_;
}

std::string StockSymbol::tencentCode() const {
  if (!valid_) return {};
  const char* prefix = exchange_ == Exchange::SSE ? "sh" : exchange_ == Exchange::SZSE ? "sz" : "bj";
  return std::string(prefix) + code_;
}
