#pragma once

#include "AppDataTypes.h"
#include "AppShell.h"
#include "DeviceLayer.h"
#include "WeatherController.h"
#include "WeatherScreen.h"

class WeatherApp final : public IApp {
 public:
  WeatherApp(DeviceLayer& device, IAppDataQueue& queue) : device_(device), controller_(queue) {}

  bool begin(const AppConfig& config);

  AppId id() const override { return AppId::WEATHER; }
  const char* name() const override { return "天气"; }
  void onEnter() override;
  void onExit() override;
  void onButton(InputEvent event) override;
  void tick(uint32_t nowMs) override;
  bool takeDirtyFlag() override;
  bool takeFullRedrawFlag() override;
  void render(bool fullRedraw) override;

 private:
  DeviceLayer& device_;
  WeatherController controller_;
  WeatherScreen screen_;
  bool initialized_ = false;
  bool active_ = false;
  bool forceDirty_ = false;
  bool forceFullRedraw_ = false;
};
