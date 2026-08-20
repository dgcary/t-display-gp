#pragma once

#include "AppDataTypes.h"
#include "AppShell.h"
#include "CryptoController.h"
#include "CryptoScreen.h"
#include "DeviceLayer.h"

class CryptoApp final : public IApp {
 public:
  CryptoApp(DeviceLayer& device, IAppDataQueue& queue) : device_(device), controller_(queue) {}
  bool begin();
  AppId id() const override { return AppId::CRYPTO; }
  const char* name() const override { return "加密货币"; }
  void onEnter() override;
  void onExit() override;
  void onButton(InputEvent event) override;
  void tick(uint32_t nowMs) override;
  bool takeDirtyFlag() override;
  bool takeFullRedrawFlag() override;
  void render(bool fullRedraw) override;
 private:
  DeviceLayer& device_;
  CryptoController controller_;
  CryptoScreen screen_;
  bool initialized_ = false;
  bool active_ = false;
  bool forceDirty_ = false;
  bool forceFullRedraw_ = false;
};
