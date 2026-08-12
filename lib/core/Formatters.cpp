#include "Formatters.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace {

std::string fixed(double value, int precision) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}

}  // namespace

std::string formatPrice(double value) {
  return fixed(value, 2);
}

std::string formatPercent(double value) {
  const std::string prefix = value > 0 ? "+" : "";
  return prefix + fixed(value, 2) + "%";
}

std::string formatVolume(uint64_t volume) {
  if (volume >= 10000) {
    return fixed(static_cast<double>(volume) / 10000.0, 1) + "万手";
  }
  return std::to_string(volume) + "手";
}

std::string formatAmount(double amount) {
  if (std::fabs(amount) >= 100000000.0) {
    return fixed(amount / 100000000.0, 2) + "亿";
  }
  if (std::fabs(amount) >= 10000.0) {
    return fixed(amount / 10000.0, 2) + "万";
  }
  return fixed(amount, 2);
}
