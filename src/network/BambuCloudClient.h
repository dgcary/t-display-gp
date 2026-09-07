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
  USER_ID_UNAVAILABLE,
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
  BambuCloudUserIdResult fetchUserId(const std::string& token,
                                     BambuRegion region) const;
  BambuCloudPrintersResult fetchPrinters(const std::string& token,
                                         BambuRegion region) const;
};
