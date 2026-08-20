#pragma once

#include "AppShell.h"
#include "DeviceLayer.h"
#include "NixieClockModel.h"
#include "NixieClockScreen.h"

class NixieClockApp final : public IApp {
 public:
  explicit NixieClockApp(DeviceLayer& device) : device_(device) {}

  bool begin();

  AppId id() const override { return AppId::NIXIE_CLOCK; }
  const char* name() const override { return "辉光时钟"; }
  void onEnter() override;
  void onExit() override;
  void onButton(InputEvent event) override;
  void tick(uint32_t nowMs) override;
  bool takeDirtyFlag() override;
  bool takeFullRedrawFlag() override;
  void render(bool fullRedraw) override;

 private:
  void refreshTime(uint32_t nowMs);

  DeviceLayer& device_;
  NixieClockScreen screen_;
  NixieClockViewModel model_;
  uint32_t lastTimeRefreshMs_ = 0;
  bool hasTimeRefresh_ = false;
  bool initialized_ = false;
  bool active_ = false;
  bool dirty_ = false;
  bool fullRedraw_ = false;
};
