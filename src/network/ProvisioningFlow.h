#pragma once

enum class ProvisioningNextAction {
  ACCEPT,
  RETRY_PORTAL,
  FAIL,
};

struct ProvisioningAttemptState {
  bool portalReturnedConnected = false;
  bool wifiConnected = false;
  bool saveAttempted = false;
  bool configValid = false;
  bool forcePortal = false;
};

constexpr ProvisioningNextAction decideProvisioningNextAction(const ProvisioningAttemptState& state) {
  if (!state.configValid) return ProvisioningNextAction::RETRY_PORTAL;

  if (state.wifiConnected) {
    if (!state.forcePortal || state.saveAttempted || state.portalReturnedConnected) {
      return ProvisioningNextAction::ACCEPT;
    }
    return ProvisioningNextAction::RETRY_PORTAL;
  }

  if (state.saveAttempted || state.portalReturnedConnected) {
    return ProvisioningNextAction::RETRY_PORTAL;
  }

  return ProvisioningNextAction::FAIL;
}
