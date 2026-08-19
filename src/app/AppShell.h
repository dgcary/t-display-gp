#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "ButtonInput.h"

enum class AppId {
  MENU,
  STOCK,
  WEATHER,
};

struct AppDescriptor {
  AppId id = AppId::MENU;
  const char* name = "";
};

struct MenuViewModel {
  std::vector<AppDescriptor> items;
  size_t selectedIndex = 0;
};

class IMenuRenderer {
 public:
  virtual ~IMenuRenderer() = default;
  virtual void render(const MenuViewModel& view, bool fullRedraw) = 0;
};

class IApp {
 public:
  virtual ~IApp() = default;
  virtual AppId id() const = 0;
  virtual const char* name() const = 0;
  virtual void onEnter() = 0;
  virtual void onExit() = 0;
  virtual void onButton(InputEvent event) = 0;
  virtual void tick(uint32_t nowMs) = 0;
  virtual bool takeDirtyFlag() = 0;
  virtual bool takeFullRedrawFlag() = 0;
  virtual void render(bool fullRedraw) = 0;
};

class MenuApp final : public IApp {
 public:
  MenuApp(std::vector<AppDescriptor> items, IMenuRenderer& renderer);

  AppId id() const override { return AppId::MENU; }
  const char* name() const override { return "菜单"; }
  void onEnter() override;
  void onExit() override;
  void onButton(InputEvent event) override;
  void tick(uint32_t nowMs) override;
  bool takeDirtyFlag() override;
  bool takeFullRedrawFlag() override;
  void render(bool fullRedraw) override;

  size_t selectedIndex() const { return selectedIndex_; }
  AppId selectedAppId() const;

 private:
  std::vector<AppDescriptor> items_;
  IMenuRenderer& renderer_;
  size_t selectedIndex_ = 0;
  bool dirty_ = true;
  bool fullRedraw_ = true;
};

class AppManager {
 public:
  AppManager(MenuApp& menu, std::initializer_list<IApp*> apps);

  bool begin(AppId startupApp = AppId::STOCK);
  void onInput(InputEvent event);
  void tick(uint32_t nowMs);
  void render();
  AppId activeAppId() const;

 private:
  IApp* findApp(AppId id) const;
  bool switchTo(AppId id);

  MenuApp& menu_;
  std::vector<IApp*> apps_;
  IApp* active_ = nullptr;
};
