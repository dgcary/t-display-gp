#include "AppConfig.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <set>
#include <utility>

namespace {

constexpr uint32_t CURRENT_SCHEMA_VERSION = 2;
constexpr size_t MAX_LOCATION_NAME_BYTES = 40;

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
      if (parsed > std::numeric_limits<uint32_t>::max()) return false;
      ++pos_;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
  }

  bool parseSigned(int32_t& value) {
    skipWs();
    bool negative = false;
    if (pos_ < text_.size() && text_[pos_] == '-') {
      negative = true;
      ++pos_;
    }
    if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) return false;

    int64_t parsed = 0;
    while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
      parsed = parsed * 10 + static_cast<unsigned>(text_[pos_] - '0');
      const int64_t limit = negative
                                ? -(static_cast<int64_t>(std::numeric_limits<int32_t>::min()))
                                : static_cast<int64_t>(std::numeric_limits<int32_t>::max());
      if (parsed > limit) return false;
      ++pos_;
    }
    const int64_t signedValue = negative ? -parsed : parsed;
    value = static_cast<int32_t>(signedValue);
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
    case ConfigValidationError::LOCATION_NAME: return "weather location name is required and must be at most 40 UTF-8 bytes";
    case ConfigValidationError::LOCATION_COORDINATES: return "weather coordinates are out of range";
    case ConfigValidationError::WEATHER_REFRESH_INTERVAL: return "weather refresh must be between 5 and 60 minutes";
  }
  return "invalid configuration";
}

ConfigValidationResult validate(const AppConfig& config) {
  if (config.schemaVersion != CURRENT_SCHEMA_VERSION) return {ConfigValidationError::SCHEMA_VERSION, 0};
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

  if (config.weather.refreshMinutes < 5 || config.weather.refreshMinutes > 60) {
    return {ConfigValidationError::WEATHER_REFRESH_INTERVAL, 0};
  }

  const bool locationPresent = config.weather.enabled || !config.location.displayName.empty() ||
                               config.location.latitudeE6 != 0 || config.location.longitudeE6 != 0;
  if (locationPresent) {
    if (config.location.displayName.empty() || config.location.displayName.size() > MAX_LOCATION_NAME_BYTES) {
      return {ConfigValidationError::LOCATION_NAME, 0};
    }
    if (config.location.latitudeE6 < -90000000 || config.location.latitudeE6 > 90000000 ||
        config.location.longitudeE6 < -180000000 || config.location.longitudeE6 > 180000000) {
      return {ConfigValidationError::LOCATION_COORDINATES, 0};
    }
  }
  return {};
}

namespace AppConfigCodec {

bool encode(const AppConfig& config, std::string& out) {
  if (!validate(config).ok()) return false;

  std::string encoded;
  encoded.reserve(256 + config.stocks.size() * 64);
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
  encoded += "]";
  encoded += ",\"location_name\":";
  appendJsonString(config.location.displayName, encoded);
  encoded += ",\"latitude_e6\":" + std::to_string(config.location.latitudeE6);
  encoded += ",\"longitude_e6\":" + std::to_string(config.location.longitudeE6);
  encoded += ",\"weather_enabled\":" + std::to_string(config.weather.enabled ? 1 : 0);
  encoded += ",\"weather_refresh_min\":" + std::to_string(config.weather.refreshMinutes);
  encoded += "}";
  out = std::move(encoded);
  return true;
}

bool decode(std::string_view json, AppConfig& out, uint32_t* sourceSchemaVersion) {
  JsonReader reader(json);
  if (!reader.consume('{')) return false;

  AppConfig parsed = AppConfig::defaults();
  bool sawSchema = false;
  bool sawRefresh = false;
  bool sawStocks = false;
  uint32_t sourceSchema = 0;

  if (reader.consume('}')) return false;
  while (true) {
    std::string key;
    if (!reader.parseString(key) || !reader.consume(':')) return false;
    if (key == "schema") {
      if (!reader.parseUnsigned(sourceSchema)) return false;
      sawSchema = true;
    } else if (key == "quote_refresh_sec") {
      if (!reader.parseUnsigned(parsed.quoteRefreshSec)) return false;
      sawRefresh = true;
    } else if (key == "stocks") {
      if (!parseStocks(reader, parsed.stocks)) return false;
      sawStocks = true;
    } else if (key == "location_name") {
      if (!reader.parseString(parsed.location.displayName)) return false;
    } else if (key == "latitude_e6") {
      if (!reader.parseSigned(parsed.location.latitudeE6)) return false;
    } else if (key == "longitude_e6") {
      if (!reader.parseSigned(parsed.location.longitudeE6)) return false;
    } else if (key == "weather_enabled") {
      uint32_t enabled = 0;
      if (!reader.parseUnsigned(enabled) || enabled > 1) return false;
      parsed.weather.enabled = enabled == 1;
    } else if (key == "weather_refresh_min") {
      if (!reader.parseUnsigned(parsed.weather.refreshMinutes)) return false;
    } else if (!reader.skipValue()) {
      return false;
    }

    if (reader.consume('}')) break;
    if (!reader.consume(',')) return false;
  }

  if (!reader.eof() || !sawSchema || !sawRefresh || !sawStocks ||
      (sourceSchema != 1 && sourceSchema != CURRENT_SCHEMA_VERSION)) {
    return false;
  }

  parsed.schemaVersion = CURRENT_SCHEMA_VERSION;
  if (!validate(parsed).ok()) return false;
  if (sourceSchemaVersion) *sourceSchemaVersion = sourceSchema;
  out = std::move(parsed);
  return true;
}

}  // namespace AppConfigCodec
