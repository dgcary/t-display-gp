#pragma once

#include "AppDataTypes.h"
#include "AppShell.h"
#include "DeviceLayer.h"
#include "HomeAssistantConfig.h"
#include "HomeAssistantController.h"
#include "HomeAssistantScreen.h"

class HomeAssistantApp final : public IApp {
 public:
  HomeAssistantApp(DeviceLayer& device, IAppDataQueue& queue) : device_(device), controller_(queue) {}
  bool begin(const HomeAssistantConfig& config);
  AppId id() const override { return AppId::HOME_ASSISTANT; }
  const char* name() const override { return "智能家居"; }
  void onEnter() override;
  void onExit() override;
  void onButton(InputEvent event) override;
  void tick(uint32_t nowMs) override;
  bool takeDirtyFlag() override;
  bool takeFullRedrawFlag() override;
  void render(bool fullRedraw) override;
 private:
  DeviceLayer& device_;
  HomeAssistantController controller_;
  HomeAssistantScreen screen_;
  bool initialized_ = false;
  bool active_ = false;
  bool forceDirty_ = false;
  bool forceFullRedraw_ = false;
};
