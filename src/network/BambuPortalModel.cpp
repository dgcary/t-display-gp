#include "BambuPortalModel.h"

std::string effectiveBambuPortalPassword(const BambuConfig& existing,
                                         const BambuPortalCredentials& input) {
  return input.password.empty() ? existing.password : input.password;
}

BambuConfig mergeBambuPortalCredentials(const BambuConfig& existing,
                                         const BambuPortalCredentials& input) {
  BambuConfig merged = existing;
  merged.enabled = input.enabled;
  merged.region = input.region;
  merged.email = input.email;

  if (!input.password.empty()) {
    if (input.rememberPassword) merged.password = input.password;
    else merged.password.clear();
  }
  return merged;
}

BambuConfig clearBambuPortalCredentials(const BambuConfig& existing) {
  BambuConfig cleared = existing;
  cleared.enabled = false;
  cleared.password.clear();
  cleared.accessToken.clear();
  cleared.cloudUserId.clear();
  cleared.printerSerial.clear();
  cleared.printerName.clear();
  return cleared;
}

BambuPortalStatus buildBambuPortalStatus(const BambuConfig& config) {
  BambuPortalStatus status;
  status.enabled = config.enabled;
  status.region = config.region;
  status.email = config.email;
  status.printerSerial = config.printerSerial;
  status.printerName = config.printerName;
  status.passwordSet = !config.password.empty();
  status.tokenSet = !config.accessToken.empty();
  return status;
}
