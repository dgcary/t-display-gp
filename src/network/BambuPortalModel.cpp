#include "BambuPortalModel.h"

#include <algorithm>

namespace {
void clearLegacyAliases(BambuConfig& config) {
  config.email.clear();
  config.password.clear();
  config.printerSerial.clear();
  config.printerName.clear();
}

void updateLegacyActiveAlias(BambuConfig& config) {
  config.printerSerial.clear();
  config.printerName.clear();
  const BambuPrinterConfig* active = activeBambuPrinter(config);
  if (!active) return;
  config.printerSerial = active->serial;
  config.printerName = active->name;
}
}  // namespace

BambuConfig mergeBambuPortalConfig(const BambuConfig& existing,
                                    const BambuPortalConfigInput& input) {
  BambuConfig merged = existing;
  const bool regionChanged = existing.region != input.region;

  merged.enabled = input.enabled;
  merged.region = input.region;

  if (!input.accessToken.empty()) {
    merged.accessToken = input.accessToken;
    merged.cloudUserId.clear();
  } else if (regionChanged) {
    merged.accessToken.clear();
    merged.cloudUserId.clear();
  }

  merged.printers = {};
  merged.printerCount = std::min(input.printerCount, BambuConfigLimits::PRINTER_COUNT);
  merged.activePrinterIndex = 0;
  for (size_t i = 0; i < merged.printerCount; ++i) {
    merged.printers[i] = input.printers[i];
    if (!input.activePrinterSerial.empty() &&
        merged.printers[i].serial == input.activePrinterSerial) {
      merged.activePrinterIndex = i;
    }
  }

  clearLegacyAliases(merged);
  updateLegacyActiveAlias(merged);
  return merged;
}

BambuConfig clearBambuPortalCredentials(const BambuConfig& existing) {
  BambuConfig cleared = existing;
  cleared.enabled = false;
  cleared.accessToken.clear();
  cleared.cloudUserId.clear();
  cleared.printers = {};
  cleared.printerCount = 0;
  cleared.activePrinterIndex = 0;
  clearLegacyAliases(cleared);
  return cleared;
}

BambuPortalStatus buildBambuPortalStatus(const BambuConfig& config) {
  BambuPortalStatus status;
  status.enabled = config.enabled;
  status.region = config.region;
  status.tokenSet = !config.accessToken.empty();
  status.printerCount = config.printerCount;
  const BambuPrinterConfig* active = activeBambuPrinter(config);
  if (active) {
    status.activePrinterSerial = active->serial;
    status.activePrinterName = active->name;
  }
  return status;
}
