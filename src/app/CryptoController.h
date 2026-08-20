#pragma once

#include <cstdint>

#include "AppDataTypes.h"

struct CryptoViewModel {
  bool wifiOnline = false;
  bool hasData = false;
  bool requestInFlight = false;
  bool stale = false;
  CryptoError error = CryptoError::NONE;
  CryptoSnapshot snapshot;
};

class CryptoController {
 public:
  explicit CryptoController(IAppDataQueue& queue) : queue_(queue) {}
  void begin();
  void setActive(bool active);
  void setWifiOnline(bool online);
  void tick(uint32_t nowMs);
  const CryptoViewModel& viewModel() const { return viewModel_; }
  bool takeDirtyFlag();
  bool takeFullRedrawFlag();

 private:
  static uint32_t elapsed(uint32_t now, uint32_t then) { return static_cast<uint32_t>(now - then); }
  bool refreshDue(uint32_t nowMs) const;
  void consumeResults(uint32_t nowMs);
  void publish(uint32_t nowMs);

  IAppDataQueue& queue_;
  CryptoSnapshot cache_;
  CryptoViewModel viewModel_;
  CryptoError lastError_ = CryptoError::NONE;
  uint32_t nextRequestId_ = 1;
  uint32_t outstandingRequestId_ = 0;
  uint32_t lastAttemptMs_ = 0;
  uint32_t lastSuccessMs_ = 0;
  bool active_ = false;
  bool wifiOnline_ = false;
  bool attempted_ = false;
  bool hasData_ = false;
  bool hasSuccessTime_ = false;
  bool dirty_ = true;
  bool fullRedraw_ = true;
};
