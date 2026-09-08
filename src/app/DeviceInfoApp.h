#pragma once

#include "AppShell.h"
#include "DeviceInfoModel.h"
#include "DeviceInfoScreen.h"
#include "DeviceLayer.h"

class DeviceInfoApp final : public IApp {
 public:
  explicit DeviceInfoApp(DeviceLayer& device) : device_(device) {}

  bool begin();

  AppId id() const override { return AppId::DEVICE_INFO; }
  const char* name() const override { return "设备信息"; }
  void onEnter() override;
  void onExit() override;
  void onButton(InputEvent event) override;
  void tick(uint32_t nowMs) override;
  bool takeDirtyFlag() override;
  bool takeFullRedrawFlag() override;
  void render(bool fullRedraw) override;

 private:
  void refreshModel(uint32_t nowMs);

  DeviceLayer& device_;
  DeviceInfoScreen screen_;
  DeviceInfoViewModel model_;
  uint32_t lastRefreshMs_ = 0;
  bool hasRefresh_ = false;
  bool initialized_ = false;
  bool active_ = false;
  bool dirty_ = false;
  bool fullRedraw_ = false;
};
