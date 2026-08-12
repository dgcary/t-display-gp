#include "AppConfig.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>
#include <utility>

namespace {

class JsonReader {
 public:
  explicit JsonReader(std::string_view text) : text_(text) {}

  bool eof() {
    skipWs();
    return pos_ == text_.size();
  }

  bool consume(char expected) {
    skipWs();
    if (pos_ >= text_.size() || text_[pos_] != expected) return false;
    ++pos_;
    return true;
  }

  bool peek(char expected) {
    skipWs();
    return pos_ < text_.size() && text_[pos_] == expected;
  }

  bool parseString(std::string& out) {
    skipWs();
    if (pos_ >= text_.size() || text_[pos_] != '"') return false;
    ++pos_;
    out.clear();
    while (pos_ < text_.size()) {
      const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
      if (c == '"') return true;
      if (c < 0x20) return false;
      if (c != '\\') {
        out.push_back(static_cast<char>(c));
        continue;
      }
      if (pos_ >= text_.size()) return false;
      const char escaped = text_[pos_++];
      switch (escaped) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          uint32_t codepoint = 0;
          if (!parseHex4(codepoint)) return false;
          if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            if (pos_ + 2 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') return false;
            pos_ += 2;
            uint32_t low = 0;
            if (!parseHex4(low) || low < 0xDC00 || low > 0xDFFF) return false;
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
          } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            return false;
          }
          if (!appendUtf8(codepoint, out)) return false;
          break;
        }
        default:
          return false;
      }
    }
    return false;
  }

  bool parseUnsigned(uint32_t& value) {
    skipWs();
    if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) return false;
    uint64_t parsed = 0;
    while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
      parsed = parsed * 10 + static_cast<unsigned>(text_[pos_] - '0');
      if (parsed > 0xFFFFFFFFu) return false;
      ++pos_;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
  }

  bool skipValue() {
    skipWs();
    if (pos_ >= text_.size()) return false;
    if (text_[pos_] == '"') {
      std::string ignored;
      return parseString(ignored);
    }
    if (text_[pos_] == '{') {
      ++pos_;
      skipWs();
      if (pos_ < text_.size() && text_[pos_] == '}') {
        ++pos_;
        return true;
      }
      while (true) {
        std::string key;
        if (!parseString(key) || !consume(':') || !skipValue()) return false;
        if (consume('}')) return true;
        if (!consume(',')) return false;
      }
    }
    if (text_[pos_] == '[') {
      ++pos_;
      skipWs();
      if (pos_ < text_.size() && text_[pos_] == ']') {
        ++pos_;
        return true;
      }
      while (true) {
        if (!skipValue()) return false;
        if (consume(']')) return true;
        if (!consume(',')) return false;
      }
    }

    const size_t start = pos_;
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ',' || c == '}' || c == ']' || std::isspace(static_cast<unsigned char>(c))) break;
      ++pos_;
    }
    return pos_ > start;
  }

 private:
  void skipWs() {
    while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) ++pos_;
  }

  bool parseHex4(uint32_t& value) {
    if (pos_ + 4 > text_.size()) return false;
    value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_++];
      value <<= 4;
      if (c >= '0' && c <= '9') value |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
      else return false;
    }
    return true;
  }

  static bool appendUtf8(uint32_t cp, std::string& out) {
    if (cp <= 0x7F) {
      out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      return false;
    }
    return true;
  }

  std::string_view text_;
  size_t pos_ = 0;
};

void appendJsonString(std::string_view value, std::string& out) {
  out.push_back('"');
  char escaped[7] = {};
  for (const unsigned char c : value) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          std::snprintf(escaped, sizeof(escaped), "\\u%04X", c);
          out += escaped;
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
}

bool parseStockObject(JsonReader& reader, ConfiguredStock& stock) {
  if (!reader.consume('{')) return false;
  bool sawSymbol = false;
  std::string rawSymbol;
  stock.displayName.clear();

  if (reader.consume('}')) return false;
  while (true) {
    std::string key;
    if (!reader.parseString(key) || !reader.consume(':')) return false;
    if (key == "symbol") {
      if (!reader.parseString(rawSymbol)) return false;
      sawSymbol = true;
    } else if (key == "name") {
      if (!reader.parseString(stock.displayName)) return false;
    } else if (!reader.skipValue()) {
      return false;
    }

    if (reader.consume('}')) break;
    if (!reader.consume(',')) return false;
  }

  if (!sawSymbol) return false;
  stock.symbol = StockSymbol::parse(rawSymbol);
  return true;
}

bool parseStocks(JsonReader& reader, std::vector<ConfiguredStock>& stocks) {
  if (!reader.consume('[')) return false;
  stocks.clear();
  if (reader.consume(']')) return true;
  while (true) {
    if (stocks.size() >= 16) return false;
    ConfiguredStock stock;
    if (!parseStockObject(reader, stock)) return false;
    stocks.push_back(std::move(stock));
    if (reader.consume(']')) return true;
    if (!reader.consume(',')) return false;
  }
}

}  // namespace

AppConfig AppConfig::defaults() {
  return {};
}

const char* ConfigValidationResult::message() const {
  switch (error) {
    case ConfigValidationError::NONE: return "ok";
    case ConfigValidationError::SCHEMA_VERSION: return "unsupported schema";
    case ConfigValidationError::STOCK_COUNT: return "configure 3 to 5 stocks";
    case ConfigValidationError::INVALID_SYMBOL: return "invalid stock symbol";
    case ConfigValidationError::DUPLICATE_SYMBOL: return "duplicate stock symbol";
    case ConfigValidationError::NAME_TOO_LONG: return "display name exceeds 30 UTF-8 bytes";
    case ConfigValidationError::REFRESH_INTERVAL: return "refresh must be 3, 4, or 5 seconds";
  }
  return "invalid configuration";
}

ConfigValidationResult validate(const AppConfig& config) {
  if (config.schemaVersion != 1) return {ConfigValidationError::SCHEMA_VERSION, 0};
  if (config.stocks.size() < 3 || config.stocks.size() > 5) {
    return {ConfigValidationError::STOCK_COUNT, 0};
  }
  if (config.quoteRefreshSec < 3 || config.quoteRefreshSec > 5) {
    return {ConfigValidationError::REFRESH_INTERVAL, 0};
  }

  std::set<std::string> seen;
  for (size_t i = 0; i < config.stocks.size(); ++i) {
    const auto& stock = config.stocks[i];
    if (!stock.symbol.valid()) return {ConfigValidationError::INVALID_SYMBOL, i};
    if (stock.displayName.size() > 30) return {ConfigValidationError::NAME_TOO_LONG, i};
    if (!seen.insert(stock.symbol.canonical()).second) {
      return {ConfigValidationError::DUPLICATE_SYMBOL, i};
    }
  }
  return {};
}

namespace AppConfigCodec {

bool encode(const AppConfig& config, std::string& out) {
  if (!validate(config).ok()) return false;

  std::string encoded;
  encoded.reserve(128 + config.stocks.size() * 64);
  encoded += "{\"schema\":" + std::to_string(config.schemaVersion);
  encoded += ",\"quote_refresh_sec\":" + std::to_string(config.quoteRefreshSec);
  encoded += ",\"stocks\":[";
  for (size_t i = 0; i < config.stocks.size(); ++i) {
    if (i != 0) encoded.push_back(',');
    encoded += "{\"symbol\":";
    appendJsonString(config.stocks[i].symbol.canonical(), encoded);
    encoded += ",\"name\":";
    appendJsonString(config.stocks[i].displayName, encoded);
    encoded.push_back('}');
  }
  encoded += "]}";
  out = std::move(encoded);
  return true;
}

bool decode(std::string_view json, AppConfig& out) {
  JsonReader reader(json);
  if (!reader.consume('{')) return false;

  AppConfig parsed = AppConfig::defaults();
  bool sawSchema = false;
  bool sawRefresh = false;
  bool sawStocks = false;

  if (reader.consume('}')) return false;
  while (true) {
    std::string key;
    if (!reader.parseString(key) || !reader.consume(':')) return false;
    if (key == "schema") {
      if (!reader.parseUnsigned(parsed.schemaVersion)) return false;
      sawSchema = true;
    } else if (key == "quote_refresh_sec") {
      if (!reader.parseUnsigned(parsed.quoteRefreshSec)) return false;
      sawRefresh = true;
    } else if (key == "stocks") {
      if (!parseStocks(reader, parsed.stocks)) return false;
      sawStocks = true;
    } else if (!reader.skipValue()) {
      return false;
    }

    if (reader.consume('}')) break;
    if (!reader.consume(',')) return false;
  }

  if (!reader.eof() || !sawSchema || !sawRefresh || !sawStocks || parsed.schemaVersion != 1) {
    return false;
  }
  out = std::move(parsed);
  return true;
}

}  // namespace AppConfigCodec
