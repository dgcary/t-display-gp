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
  TWO_FACTOR_REQUIRED,
  USER_ID_UNAVAILABLE,
};

struct BambuCloudLoginResult {
  BambuCloudError error = BambuCloudError::NETWORK;
  std::string accessToken;
  bool ok() const { return error == BambuCloudError::NONE; }
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
  BambuCloudUserIdResult fetchUserId(const std::string& token,
                                     BambuRegion region) const;
  BambuCloudPrintersResult fetchPrinters(const std::string& token,
                                         BambuRegion region) const;
};
