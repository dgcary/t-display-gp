#include "TencentParser.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace Field {
constexpr size_t NAME = 1;
constexpr size_t CODE = 2;
constexpr size_t LAST = 3;
constexpr size_t PREV_CLOSE = 4;
constexpr size_t OPEN = 5;
constexpr size_t VOLUME = 6;
constexpr size_t TIMESTAMP = 30;
constexpr size_t CHANGE = 31;
constexpr size_t PERCENT = 32;
constexpr size_t HIGH = 33;
constexpr size_t LOW = 34;
constexpr size_t AMOUNT_WAN = 37;
constexpr size_t REQUIRED_COUNT = AMOUNT_WAN + 1;
}  // namespace Field

bool parseDouble(std::string_view token, double& value) {
  if (token.empty()) return false;
  std::string text(token);
  char* end = nullptr;
  errno = 0;
  const double parsed = std::strtod(text.c_str(), &end);
  if (errno != 0 || end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
  value = parsed;
  return true;
}

bool parseUint64(std::string_view token, uint64_t& value) {
  double parsed = 0;
  if (!parseDouble(token, parsed) || parsed < 0 || std::floor(parsed) != parsed ||
      parsed > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
    return false;
  }
  value = static_cast<uint64_t>(parsed);
  return true;
}

bool leapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month) {
  static constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && leapYear(year)) return 29;
  return month >= 1 && month <= 12 ? days[month - 1] : 0;
}

int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear = (153U * ((month > 2 ? month - 3U : month + 9U)) + 2U) / 5U + day - 1U;
  const unsigned dayOfEra = yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

bool parseChinaTimestamp(std::string_view token, uint64_t& epochSeconds) {
  if (token.size() != 14) return false;
  for (const char c : token) {
    if (c < '0' || c > '9') return false;
  }
  const auto part = [&](size_t pos, size_t len) {
    int value = 0;
    for (size_t i = 0; i < len; ++i) value = value * 10 + (token[pos + i] - '0');
    return value;
  };
  const int year = part(0, 4);
  const int month = part(4, 2);
  const int day = part(6, 2);
  const int hour = part(8, 2);
  const int minute = part(10, 2);
  const int second = part(12, 2);
  if (year < 1970 || month < 1 || month > 12 || day < 1 || day > daysInMonth(year, month) ||
      hour > 23 || minute > 59 || second > 59) {
    return false;
  }
  const int64_t localSeconds = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400LL +
                               hour * 3600LL + minute * 60LL + second;
  constexpr int64_t CHINA_UTC_OFFSET_SECONDS = 8LL * 3600LL;
  const int64_t utcSeconds = localSeconds - CHINA_UTC_OFFSET_SECONDS;
  if (utcSeconds < 0) return false;
  epochSeconds = static_cast<uint64_t>(utcSeconds);
  return true;
}

std::vector<std::string_view> splitFields(std::string_view payload) {
  std::vector<std::string_view> fields;
  size_t start = 0;
  while (start <= payload.size()) {
    const size_t delimiter = payload.find('~', start);
    if (delimiter == std::string_view::npos) {
      fields.push_back(payload.substr(start));
      break;
    }
    fields.push_back(payload.substr(start, delimiter - start));
    start = delimiter + 1;
  }
  return fields;
}

bool looksLikeTencentAssignment(std::string_view body, std::string_view& variable,
                                std::string_view& payload) {
  const size_t equals = body.find("=\"");
  if (equals == std::string_view::npos || body.size() < equals + 4) return false;
  variable = body.substr(0, equals);
  const size_t payloadBegin = equals + 2;
  const size_t close = body.rfind("\";");
  if (close == std::string_view::npos || close < payloadBegin) return false;
  payload = body.substr(payloadBegin, close - payloadBegin);
  for (size_t i = close + 2; i < body.size(); ++i) {
    const char c = body[i];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
  }
  return true;
}

}  // namespace

namespace TencentParser {

ProviderError parseQuote(std::string_view body, const StockSymbol& symbol, QuoteSnapshot& out) {
  if (!symbol.valid()) return ProviderError::UNSUPPORTED;

  std::string_view variable;
  std::string_view payload;
  if (!looksLikeTencentAssignment(body, variable, payload)) return ProviderError::PARSE;

  const std::string expectedVariable = "v_" + symbol.tencentCode();
  if (variable != expectedVariable) return ProviderError::STALE;

  const auto fields = splitFields(payload);
  if (fields.size() < Field::REQUIRED_COUNT) return ProviderError::MISSING_FIELD;
  if (fields[Field::CODE] != symbol.code()) return ProviderError::STALE;
  if (fields[Field::NAME].empty()) return ProviderError::MISSING_FIELD;

  QuoteSnapshot parsed;
  parsed.symbol = symbol;
  parsed.name.assign(fields[Field::NAME]);
  parsed.provider = ProviderId::TENCENT;

  double amountWan = 0;
  if (!parseDouble(fields[Field::LAST], parsed.last) ||
      !parseDouble(fields[Field::PREV_CLOSE], parsed.prevClose) ||
      !parseDouble(fields[Field::OPEN], parsed.open) ||
      !parseUint64(fields[Field::VOLUME], parsed.volume) ||
      !parseDouble(fields[Field::CHANGE], parsed.change) ||
      !parseDouble(fields[Field::PERCENT], parsed.changePercent) ||
      !parseDouble(fields[Field::HIGH], parsed.high) ||
      !parseDouble(fields[Field::LOW], parsed.low) ||
      !parseDouble(fields[Field::AMOUNT_WAN], amountWan) ||
      !parseChinaTimestamp(fields[Field::TIMESTAMP], parsed.epochSeconds)) {
    return ProviderError::PARSE;
  }

  if (parsed.last <= 0 || parsed.prevClose < 0 || parsed.open < 0 || parsed.high < 0 ||
      parsed.low < 0 || amountWan < 0) {
    return ProviderError::PARSE;
  }
  parsed.amount = amountWan * 10000.0;

  out = std::move(parsed);
  return ProviderError::NONE;
}

}  // namespace TencentParser
