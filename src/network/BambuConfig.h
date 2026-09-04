#pragma once

#include <cstddef>
#include <string>

enum class BambuRegion {
  US_EU = 0,
  CHINA = 1,
};

enum class BambuConfigError {
  NONE = 0,
  EMAIL_REQUIRED,
  CREDENTIAL_REQUIRED,
  EMAIL_INVALID,
  EMAIL_TOO_LONG,
  PASSWORD_TOO_LONG,
  TOKEN_TOO_LONG,
  USER_ID_TOO_LONG,
  SERIAL_TOO_LONG,
  NAME_TOO_LONG,
  ENCODED_TOO_LONG,
  MALFORMED,
};

struct BambuConfigValidationResult {
  BambuConfigError error = BambuConfigError::NONE;
  bool ok() const { return error == BambuConfigError::NONE; }
};

struct BambuConfig {
  bool enabled = false;
  BambuRegion region = BambuRegion::US_EU;
  std::string email;
  std::string password;
  std::string accessToken;
  std::string cloudUserId;
  std::string printerSerial;
  std::string printerName;
};

namespace BambuConfigLimits {
constexpr size_t EMAIL = 160;
constexpr size_t PASSWORD = 256;
constexpr size_t ACCESS_TOKEN = 1536;
constexpr size_t CLOUD_USER_ID = 96;
constexpr size_t PRINTER_SERIAL = 32;
constexpr size_t PRINTER_NAME = 64;
constexpr size_t ENCODED = 4096;
}  // namespace BambuConfigLimits

BambuConfigValidationResult validateBambuConfig(const BambuConfig& config);
const char* bambuBrokerForRegion(BambuRegion region);

class BambuConfigCodec {
 public:
  static bool encode(const BambuConfig& config, std::string& out);
  static bool decode(const std::string& encoded, BambuConfig& out);
};
