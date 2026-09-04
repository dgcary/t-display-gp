#pragma once

#include <string>

#include "BambuConfig.h"

struct BambuPortalCredentials {
  bool enabled = false;
  BambuRegion region = BambuRegion::US_EU;
  std::string email;
  std::string password;
  bool rememberPassword = true;
};

struct BambuPortalStatus {
  bool enabled = false;
  BambuRegion region = BambuRegion::US_EU;
  std::string email;
  std::string printerSerial;
  std::string printerName;
  bool passwordSet = false;
  bool tokenSet = false;
};

std::string effectiveBambuPortalPassword(const BambuConfig& existing,
                                         const BambuPortalCredentials& input);
BambuConfig mergeBambuPortalCredentials(const BambuConfig& existing,
                                         const BambuPortalCredentials& input);
BambuConfig clearBambuPortalCredentials(const BambuConfig& existing);
BambuPortalStatus buildBambuPortalStatus(const BambuConfig& config);
