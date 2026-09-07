#pragma once

#include <string>
#include <vector>

#include "BambuCloudProtocol.h"
#include "BambuConfig.h"

enum class BambuCloudError {
  NONE = 0,
  NETWORK,
  TLS,
  HTTP_STATUS,
  BODY_TOO_LARGE,
  TRUNCATED_BODY,
  MALFORMED,
  INVALID_CREDENTIALS,
  VERIFICATION_REQUIRED,
  RATE_LIMITED,
  USER_ID_UNAVAILABLE,
};

enum class BambuVerificationType {
  NONE = 0,
  EMAIL_CODE,
  SMS_CODE,
  TFA,
};

struct BambuCloudLoginResult {
  BambuCloudError error = BambuCloudError::NETWORK;
  std::string accessToken;
  BambuVerificationType verificationType = BambuVerificationType::NONE;
  std::string tfaKey;
  int httpStatus = 0;
  bool ok() const { return error == BambuCloudError::NONE; }
  bool verificationRequired() const { return error == BambuCloudError::VERIFICATION_REQUIRED; }
};

struct BambuCloudUserIdResult {
  BambuCloudError error = BambuCloudError::NETWORK;
  std::string userId;
  bool ok() const { return error == BambuCloudError::NONE; }
};

struct BambuCloudPrintersResult {
  BambuCloudError error = BambuCloudError::NETWORK;
  std::vector<BambuCloudDevice> printers;
  bool ok() const { return error == BambuCloudError::NONE; }
};

class BambuCloudClient {
 public:
  BambuCloudLoginResult login(const std::string& email,
                              const std::string& password,
                              BambuRegion region) const;
  BambuCloudLoginResult submitVerificationCode(const std::string& email,
                                               const std::string& code,
                                               BambuRegion region) const;
  BambuCloudError requestVerificationCode(const std::string& email,
                                          BambuRegion region) const;
  BambuCloudLoginResult submitTfaCode(const std::string& tfaKey,
                                      const std::string& code,
                                      BambuRegion region) const;
  BambuCloudUserIdResult fetchUserId(const std::string& token,
                                     BambuRegion region) const;
  BambuCloudPrintersResult fetchPrinters(const std::string& token,
                                         BambuRegion region) const;
};
