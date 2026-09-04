#pragma once

#include <cstdint>

#include "AppShell.h"
#include "BambuConfig.h"
#include "BambuMqttService.h"
#include "BambuScreen.h"
#include "DeviceLayer.h"

class BambuApp final : public IApp {
 public:
  BambuApp(DeviceLayer& device, BambuMqttService& service, BambuConfig& config)
      : device_(device), service_(service), config_(config) {}

  bool begin();
  AppId id() const override { return AppId::BAMBU; }
  const char* name() const override { return "Bambu Lab"; }
  void onEnter() override;
  void onExit() override;
  void onButton(InputEvent event) override;
  void tick(uint32_t nowMs) override;
  bool takeDirtyFlag() override;
  bool takeFullRedrawFlag() override;
  void render(bool fullRedraw) override;

 private:
  DeviceLayer& device_;
  BambuMqttService& service_;
  BambuConfig& config_;
  BambuScreen screen_;
  BambuViewModel model_;
  bool initialized_ = false;
  bool active_ = false;
  bool dirty_ = false;
  bool fullRedraw_ = false;
  bool hasRefresh_ = false;
  uint32_t lastRefreshMs_ = 0U;
};
