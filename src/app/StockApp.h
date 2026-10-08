#pragma once

#include "AppShell.h"
#include "DeviceLayer.h"
#include "MarketDataWorker.h"
#include "StockController.h"
#include "StockScreen.h"

class StockApp final : public IApp {
 public:
  static constexpr size_t MAX_NAVIGATION_PAGES = 4U;

  explicit StockApp(DeviceLayer& device) : device_(device), controller_(worker_) {}

  bool begin(const AppConfig& config);

  AppId id() const override { return AppId::STOCK; }
  const char* name() const override { return "股票"; }
  void onEnter() override;
  void onExit() override;
  void onButton(InputEvent event) override;
  void tick(uint32_t nowMs) override;
  bool takeDirtyFlag() override;
  bool takeFullRedrawFlag() override;
  void render(bool fullRedraw) override;
  size_t pageCount() const override {
    const size_t count = controller_.viewModel().count;
    return count < MAX_NAVIGATION_PAGES ? count : MAX_NAVIGATION_PAGES;
  }
  bool selectPage(size_t pageIndex) override;
  size_t selectedPage() const override { return controller_.viewModel().index; }

 private:
  DeviceLayer& device_;
  MarketDataWorker worker_;
  StockController controller_;
  StockScreen screen_;
  bool initialized_ = false;
  bool active_ = false;
  bool forceDirty_ = false;
  bool forceFullRedraw_ = false;
};
