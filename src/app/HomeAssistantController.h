#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "AppDataTypes.h"
#include "HomeAssistantConfig.h"

struct HomeAssistantEntityViewModel {
  std::string label;
  std::string entityId;
  std::string state;
  std::string unit;
  bool hasData = false;
  HomeAssistantError error = HomeAssistantError::NONE;
};

struct HomeAssistantViewModel {
  bool configured = false;
  bool wifiOnline = false;
  bool requestInFlight = false;
  bool stale = false;
  size_t entityCount = 0;
  std::array<HomeAssistantEntityViewModel, 4> entities{};
};

class HomeAssistantController {
 public:
  explicit HomeAssistantController(IAppDataQueue& queue) : queue_(queue) {}
  void begin(const HomeAssistantConfig& config);
  void setActive(bool active);
  void setWifiOnline(bool online);
  void tick(uint32_t nowMs);
  const HomeAssistantViewModel& viewModel() const { return viewModel_; }
  bool takeDirtyFlag();
  bool takeFullRedrawFlag();

 private:
  static uint32_t elapsed(uint32_t now, uint32_t then) { return static_cast<uint32_t>(now - then); }
  bool configured() const;
  bool refreshDue(uint32_t nowMs) const;
  void consumeResults();
  bool enqueueEntity(uint8_t index, uint32_t nowMs);
  void startCycle(uint32_t nowMs);
  void publish(uint32_t nowMs);

  IAppDataQueue& queue_;
  HomeAssistantConfig config_;
  HomeAssistantViewModel viewModel_;
  std::array<HomeAssistantEntitySnapshot, 4> cache_{};
  std::array<bool, 4> hasData_{};
  std::array<HomeAssistantError, 4> errors_{};
  uint32_t nextRequestId_ = 1;
  uint32_t outstandingRequestId_ = 0;
  uint32_t lastAttemptMs_ = 0;
  uint32_t lastSuccessMs_ = 0;
  uint8_t outstandingEntityIndex_ = 0;
  uint8_t nextEntityIndex_ = 0;
  bool active_ = false;
  bool wifiOnline_ = false;
  bool attempted_ = false;
  bool cycleInProgress_ = false;
  bool hasSuccessTime_ = false;
  bool dirty_ = true;
  bool fullRedraw_ = true;
};
