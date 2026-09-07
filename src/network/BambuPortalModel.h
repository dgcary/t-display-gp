#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "BambuConfig.h"

struct BambuPortalConfigInput {
  bool enabled = false;
  BambuRegion region = BambuRegion::US_EU;
  std::string accessToken;  // blank = preserve when region is unchanged
  std::array<BambuPrinterConfig, BambuConfigLimits::PRINTER_COUNT> printers{};
  size_t printerCount = 0;
  std::string activePrinterSerial;
};

struct BambuPortalStatus {
  bool enabled = false;
  BambuRegion region = BambuRegion::US_EU;
  bool tokenSet = false;
  size_t printerCount = 0;
  std::string activePrinterSerial;
  std::string activePrinterName;
};

BambuConfig mergeBambuPortalConfig(const BambuConfig& existing,
                                    const BambuPortalConfigInput& input);
BambuConfig clearBambuPortalCredentials(const BambuConfig& existing);
BambuPortalStatus buildBambuPortalStatus(const BambuConfig& config);
