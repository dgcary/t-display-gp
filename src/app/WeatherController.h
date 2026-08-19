#pragma once

#include <cstdint>
#include <string>

#include "AppConfig.h"
#include "AppDataTypes.h"

struct WeatherViewModel {
  bool configured = false;
  bool wifiOnline = false;
  bool hasData = false;
  bool requestInFlight = false;
  bool stale = false;
  WeatherError error = WeatherError::NONE;
  std::string locationName;
  WeatherSnapshot weather;
};

class WeatherController {
 public:
  explicit WeatherController(IAppDataQueue& queue) : queue_(queue) {}

  void begin(const AppConfig& config);
  void setActive(bool active);
  void setWifiOnline(bool online);
  void tick(uint32_t nowMs);

  const WeatherViewModel& viewModel() const { return viewModel_; }
  bool takeDirtyFlag();
  bool takeFullRedrawFlag();

 private:
  static uint32_t elapsed(uint32_t nowMs, uint32_t thenMs) {
    return static_cast<uint32_t>(nowMs - thenMs);
  }
  bool configured() const;
  bool refreshDue(uint32_t nowMs) const;
  void consumeResults(uint32_t nowMs);
  void publish(uint32_t nowMs);

  IAppDataQueue& queue_;
  LocationConfig location_;
  WeatherConfig config_;
  WeatherSnapshot cache_;
  WeatherViewModel viewModel_;
  WeatherError lastError_ = WeatherError::NONE;
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
