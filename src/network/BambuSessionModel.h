#pragma once

#include <cstdint>

enum class BambuSessionState {
  DISABLED = 0,
  UNCONFIGURED,
  PRINTER_SELECTION_REQUIRED,
  MQTT_CONNECTING,
  ONLINE,
  TOKEN_INVALID,
  RELOGIN_PENDING,
  RELOGIN_IN_PROGRESS,
  TWO_FACTOR_REQUIRED,
  LOGIN_FAILED,
  NETWORK_ERROR,
  BUFFER_ERROR,
};

enum class BambuReloginFailure {
  INVALID_CREDENTIALS = 0,
  TWO_FACTOR_REQUIRED,
  NETWORK,
  SERVICE_ERROR,
};

class BambuSessionModel {
 public:
  void setAutomaticReloginAvailable(bool available);

  void onMqttAuthFailure(uint32_t nowMs);
  bool shouldRelogin(uint32_t nowMs) const;
  void onReloginStarted();
  void onReloginSuccess();
  void onReloginFailure(uint32_t nowMs, BambuReloginFailure reason);

  BambuSessionState state() const { return state_; }
  uint8_t reloginFailureCount() const { return reloginFailureCount_; }
  uint32_t reloginDelayMs() const { return reloginDelayMs_; }

 private:
  BambuSessionState state_ = BambuSessionState::UNCONFIGURED;
  bool automaticReloginAvailable_ = false;
  bool reloginScheduled_ = false;
  uint8_t reloginFailureCount_ = 0U;
  uint32_t reloginAnchorMs_ = 0;
  uint32_t reloginDelayMs_ = 0U;
};
