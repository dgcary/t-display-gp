#pragma once

#include <string>
#include <string_view>
#include <utility>

enum class Exchange { SSE, SZSE, BSE, UNKNOWN };

class StockSymbol {
 public:
  StockSymbol() = default;

  static StockSymbol parse(std::string_view raw);

  bool valid() const { return valid_; }
  const std::string& code() const { return code_; }
  Exchange exchange() const { return exchange_; }

  std::string canonical() const;
  std::string eastMoneySecId() const;
  std::string tencentCode() const;

  bool operator==(const StockSymbol& other) const {
    return valid_ == other.valid_ && code_ == other.code_ && exchange_ == other.exchange_;
  }

 private:
  StockSymbol(std::string code, Exchange exchange)
      : code_(std::move(code)), exchange_(exchange), valid_(exchange != Exchange::UNKNOWN) {}

  std::string code_;
  Exchange exchange_ = Exchange::UNKNOWN;
  bool valid_ = false;
};
