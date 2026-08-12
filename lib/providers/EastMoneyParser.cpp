#include "EastMoneyParser.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace {

struct JsonSlice {
  std::string_view value;
  bool quoted = false;
};

size_t skipWs(std::string_view text, size_t pos) {
  while (pos < text.size()) {
    const char c = text[pos];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
    ++pos;
  }
  return pos;
}

bool structurallyJsonObject(std::string_view text) {
  size_t begin = skipWs(text, 0);
  if (begin >= text.size() || text[begin] != '{') return false;

  size_t end = text.size();
  while (end > begin) {
    const char c = text[end - 1];
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
    --end;
  }
  if (end <= begin || text[end - 1] != '}') return false;

  int braces = 0;
  int brackets = 0;
  bool inString = false;
  bool escaped = false;
  for (size_t i = begin; i < end; ++i) {
    const char c = text[i];
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
    } else if (c == '{') {
      ++braces;
    } else if (c == '}') {
      if (--braces < 0) return false;
    } else if (c == '[') {
      ++brackets;
    } else if (c == ']') {
      if (--brackets < 0) return false;
    }
  }
  return !inString && braces == 0 && brackets == 0;
}

bool findMatching(std::string_view text, size_t openPos, char openChar, char closeChar,
                  size_t& closePos) {
  int depth = 0;
  bool inString = false;
  bool escaped = false;
  for (size_t i = openPos; i < text.size(); ++i) {
    const char c = text[i];
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
    } else if (c == openChar) {
      ++depth;
    } else if (c == closeChar) {
      --depth;
      if (depth == 0) {
        closePos = i;
        return true;
      }
      if (depth < 0) return false;
    }
  }
  return false;
}

bool findKey(std::string_view object, std::string_view key, size_t& valuePos) {
  const std::string needle = "\"" + std::string(key) + "\"";
  size_t pos = 0;
  while ((pos = object.find(needle, pos)) != std::string_view::npos) {
    size_t colon = skipWs(object, pos + needle.size());
    if (colon < object.size() && object[colon] == ':') {
      valuePos = skipWs(object, colon + 1);
      return valuePos < object.size();
    }
    pos += needle.size();
  }
  return false;
}

bool dataObject(std::string_view body, std::string_view& data) {
  size_t valuePos = 0;
  if (!findKey(body, "data", valuePos) || valuePos >= body.size() || body[valuePos] != '{') {
    return false;
  }
  size_t close = 0;
  if (!findMatching(body, valuePos, '{', '}', close)) return false;
  data = body.substr(valuePos, close - valuePos + 1);
  return true;
}

bool extractSlice(std::string_view object, std::string_view key, JsonSlice& out) {
  size_t pos = 0;
  if (!findKey(object, key, pos)) return false;
  if (object[pos] == '"') {
    const size_t begin = ++pos;
    bool escaped = false;
    for (; pos < object.size(); ++pos) {
      const char c = object[pos];
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        out = {object.substr(begin, pos - begin), true};
        return true;
      }
    }
    return false;
  }

  const size_t begin = pos;
  while (pos < object.size()) {
    const char c = object[pos];
    if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      break;
    }
    ++pos;
  }
  if (pos == begin) return false;
  out = {object.substr(begin, pos - begin), false};
  return true;
}

bool decodeJsonString(std::string_view raw, std::string& out) {
  out.clear();
  out.reserve(raw.size());
  for (size_t i = 0; i < raw.size(); ++i) {
    const char c = raw[i];
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (++i >= raw.size()) return false;
    switch (raw[i]) {
      case '"': out.push_back('"'); break;
      case '\\': out.push_back('\\'); break;
      case '/': out.push_back('/'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      default:
        return false;  // Unicode escapes are not expected from current provider payloads.
    }
  }
  return true;
}

bool parseDouble(JsonSlice slice, double& value) {
  if (slice.value.empty()) return false;
  std::string token(slice.value);
  char* end = nullptr;
  errno = 0;
  const double parsed = std::strtod(token.c_str(), &end);
  if (errno != 0 || end == token.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
  value = parsed;
  return true;
}

bool parseUint64(JsonSlice slice, uint64_t& value) {
  double parsed = 0;
  if (!parseDouble(slice, parsed) || parsed < 0 || std::floor(parsed) != parsed ||
      parsed > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
    return false;
  }
  value = static_cast<uint64_t>(parsed);
  return true;
}

bool decodeScaledNumber(JsonSlice slice, double& value) {
  double parsed = 0;
  if (!parseDouble(slice, parsed)) return false;
  const bool integerLike = slice.value.find_first_of(".eE") == std::string_view::npos;
  value = integerLike ? parsed / 100.0 : parsed;
  return std::isfinite(value);
}

bool parseRequiredNumber(std::string_view object, std::string_view key, double& value,
                         bool scaled = false) {
  JsonSlice slice;
  if (!extractSlice(object, key, slice)) return false;
  return scaled ? decodeScaledNumber(slice, value) : parseDouble(slice, value);
}

bool parseRequiredUint(std::string_view object, std::string_view key, uint64_t& value) {
  JsonSlice slice;
  return extractSlice(object, key, slice) && parseUint64(slice, value);
}

bool parseStringField(std::string_view object, std::string_view key, std::string& value) {
  JsonSlice slice;
  return extractSlice(object, key, slice) && slice.quoted && decodeJsonString(slice.value, value);
}

bool splitCsv(std::string_view line, std::vector<std::string_view>& fields) {
  fields.clear();
  size_t start = 0;
  while (start <= line.size()) {
    const size_t comma = line.find(',', start);
    if (comma == std::string_view::npos) {
      fields.push_back(line.substr(start));
      break;
    }
    fields.push_back(line.substr(start, comma - start));
    start = comma + 1;
  }
  return fields.size() >= 8;
}

bool parseMinute(std::string_view timestamp, uint16_t& minuteOfDay) {
  if (timestamp.size() < 16 || timestamp[10] != ' ' || timestamp[13] != ':') return false;
  const char h0 = timestamp[11], h1 = timestamp[12], m0 = timestamp[14], m1 = timestamp[15];
  if (h0 < '0' || h0 > '9' || h1 < '0' || h1 > '9' || m0 < '0' || m0 > '9' || m1 < '0' || m1 > '9') {
    return false;
  }
  const int hour = (h0 - '0') * 10 + (h1 - '0');
  const int minute = (m0 - '0') * 10 + (m1 - '0');
  if (hour > 23 || minute > 59) return false;
  minuteOfDay = static_cast<uint16_t>(hour * 60 + minute);
  return true;
}

bool inTradingSession(uint16_t minute) {
  return (minute >= 9 * 60 + 30 && minute <= 11 * 60 + 30) ||
         (minute >= 13 * 60 && minute <= 15 * 60);
}

bool parseFloatToken(std::string_view token, float& out) {
  double value = 0;
  if (!parseDouble({token, false}, value) || value < 0 ||
      value > static_cast<double>(std::numeric_limits<float>::max())) {
    return false;
  }
  out = static_cast<float>(value);
  return true;
}

bool parseVolumeToken(std::string_view token, uint32_t& out) {
  uint64_t value = 0;
  if (!parseUint64({token, false}, value) || value > std::numeric_limits<uint32_t>::max()) return false;
  out = static_cast<uint32_t>(value);
  return true;
}

bool extractStringArray(std::string_view object, std::string_view key, std::vector<std::string>& out) {
  size_t valuePos = 0;
  if (!findKey(object, key, valuePos) || object[valuePos] != '[') return false;
  size_t close = 0;
  if (!findMatching(object, valuePos, '[', ']', close)) return false;

  out.clear();
  size_t pos = valuePos + 1;
  while (true) {
    pos = skipWs(object, pos);
    if (pos >= close) return true;
    if (object[pos] == ',') {
      ++pos;
      continue;
    }
    if (object[pos] != '"') return false;
    const size_t begin = ++pos;
    bool escaped = false;
    for (; pos < close; ++pos) {
      const char c = object[pos];
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        std::string decoded;
        if (!decodeJsonString(object.substr(begin, pos - begin), decoded)) return false;
        out.push_back(std::move(decoded));
        ++pos;
        break;
      }
    }
    if (pos > close) return false;
  }
}

}  // namespace

namespace EastMoneyParser {

ProviderError parseQuote(std::string_view body, const StockSymbol& symbol, QuoteSnapshot& out) {
  if (!symbol.valid()) return ProviderError::UNSUPPORTED;
  if (!structurallyJsonObject(body)) return ProviderError::PARSE;

  std::string_view data;
  if (!dataObject(body, data)) return ProviderError::MISSING_FIELD;

  QuoteSnapshot parsed;
  parsed.symbol = symbol;
  parsed.provider = ProviderId::EAST_MONEY;

  std::string providerCode;
  if (!parseStringField(data, "f57", providerCode) || !parseStringField(data, "f58", parsed.name)) {
    return ProviderError::MISSING_FIELD;
  }
  if (providerCode != symbol.code()) return ProviderError::STALE;

  if (!parseRequiredNumber(data, "f43", parsed.last, true)) return ProviderError::PARSE;
  if (!parseRequiredNumber(data, "f44", parsed.high, true)) return ProviderError::MISSING_FIELD;
  if (!parseRequiredNumber(data, "f45", parsed.low, true)) return ProviderError::MISSING_FIELD;
  if (!parseRequiredNumber(data, "f46", parsed.open, true)) return ProviderError::MISSING_FIELD;
  if (!parseRequiredUint(data, "f47", parsed.volume)) return ProviderError::MISSING_FIELD;
  if (!parseRequiredNumber(data, "f48", parsed.amount)) return ProviderError::MISSING_FIELD;
  if (!parseRequiredNumber(data, "f60", parsed.prevClose, true)) return ProviderError::MISSING_FIELD;
  if (!parseRequiredNumber(data, "f169", parsed.change, true)) return ProviderError::MISSING_FIELD;
  if (!parseRequiredNumber(data, "f170", parsed.changePercent, true)) return ProviderError::MISSING_FIELD;
  if (!parseRequiredUint(data, "f86", parsed.epochSeconds)) return ProviderError::MISSING_FIELD;

  if (parsed.last <= 0 || parsed.high < 0 || parsed.low < 0 || parsed.open < 0 || parsed.prevClose < 0 ||
      parsed.amount < 0) {
    return ProviderError::PARSE;
  }

  out = std::move(parsed);
  return ProviderError::NONE;
}

ProviderError parseIntraday(std::string_view body, const StockSymbol& symbol, IntradaySeries& out) {
  if (!symbol.valid()) return ProviderError::UNSUPPORTED;
  if (!structurallyJsonObject(body)) return ProviderError::PARSE;

  std::string_view data;
  if (!dataObject(body, data)) return ProviderError::MISSING_FIELD;

  size_t trendsPos = 0;
  if (!findKey(data, "trends", trendsPos)) return ProviderError::MISSING_FIELD;

  std::vector<std::string> rows;
  if (!extractStringArray(data, "trends", rows)) return ProviderError::PARSE;

  IntradaySeries parsed;
  parsed.reserve(rows.size() < 242 ? rows.size() : 242);
  std::vector<std::string_view> fields;
  uint16_t lastMinute = 0;
  bool hasLastMinute = false;

  for (const auto& row : rows) {
    if (!splitCsv(row, fields)) return ProviderError::PARSE;
    uint16_t minute = 0;
    if (!parseMinute(fields[0], minute)) return ProviderError::PARSE;
    if (!inTradingSession(minute)) continue;
    if (hasLastMinute && minute <= lastMinute) continue;

    IntradayPoint point;
    point.minuteOfDay = minute;
    if (!parseFloatToken(fields[1], point.price) || point.price <= 0 ||
        !parseVolumeToken(fields[5], point.volume) ||
        !parseFloatToken(fields[7], point.averagePrice) || point.averagePrice <= 0) {
      return ProviderError::PARSE;
    }

    parsed.push_back(point);
    lastMinute = minute;
    hasLastMinute = true;
    if (parsed.size() >= 242) break;
  }

  out = std::move(parsed);
  return ProviderError::NONE;
}

}  // namespace EastMoneyParser
