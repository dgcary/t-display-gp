#pragma once

#include <array>
#include <cstddef>
#include <string>

enum class BambuRegion {
  US_EU = 0,
  CHINA = 1,
};

enum class BambuConfigError {
  NONE = 0,
  TOKEN_REQUIRED,
  USER_ID_REQUIRED,
  PRINTER_REQUIRED,
  TOKEN_TOO_LONG,
  USER_ID_TOO_LONG,
  TOO_MANY_PRINTERS,
  SERIAL_REQUIRED,
  SERIAL_INVALID,
  SERIAL_TOO_LONG,
  NAME_TOO_LONG,
  DUPLICATE_SERIAL,
  ACTIVE_PRINTER_INVALID,
  ENCODED_TOO_LONG,
  MALFORMED,
};

struct BambuConfigValidationResult {
  BambuConfigError error = BambuConfigError::NONE;
  bool ok() const { return error == BambuConfigError::NONE; }
};

struct BambuPrinterConfig {
  std::string serial;
  std::string name;
};

namespace BambuConfigLimits {
constexpr size_t ACCESS_TOKEN = 1536;
constexpr size_t CLOUD_USER_ID = 96;
constexpr size_t PRINTER_SERIAL = 32;
constexpr size_t PRINTER_NAME = 64;
constexpr size_t PRINTER_COUNT = 4;
constexpr size_t ENCODED = 4096;
}  // namespace BambuConfigLimits

struct BambuConfig {
  bool enabled = false;
  BambuRegion region = BambuRegion::US_EU;
  std::string accessToken;
  std::string cloudUserId;
  std::array<BambuPrinterConfig, BambuConfigLimits::PRINTER_COUNT> printers{};
  size_t printerCount = 0;
  size_t activePrinterIndex = 0;
};

BambuConfigValidationResult validateBambuConfig(const BambuConfig& config);
const BambuPrinterConfig* activeBambuPrinter(const BambuConfig& config);
bool selectRelativeBambuPrinter(BambuConfig& config, int direction);
const char* bambuBrokerForRegion(BambuRegion region);

class BambuConfigCodec {
 public:
  static bool encode(const BambuConfig& config, std::string& out);
  static bool decode(const std::string& encoded, BambuConfig& out);
};
