#include "AppShell.h"

#include <utility>

MenuApp::MenuApp(std::vector<AppDescriptor> items, IMenuRenderer& renderer)
    : items_(std::move(items)), renderer_(renderer) {}

void MenuApp::onEnter() {
  dirty_ = true;
  fullRedraw_ = true;
}

void MenuApp::onExit() {
  ++exitCount_;
}

void MenuApp::onButton(InputEvent event) {
  if (items_.empty()) return;
  if (event == InputEvent::PREV_SHORT) {
    selectedIndex_ = selectedIndex_ == 0 ? items_.size() - 1 : selectedIndex_ - 1;
    dirty_ = true;
  } else if (event == InputEvent::NEXT_SHORT) {
    selectedIndex_ = (selectedIndex_ + 1) % items_.size();
    dirty_ = true;
  }
}

void MenuApp::tick(uint32_t) {}

bool MenuApp::takeDirtyFlag() {
  const bool value = dirty_;
  dirty_ = false;
  return value;
}

bool MenuApp::takeFullRedrawFlag() {
  const bool value = fullRedraw_;
  fullRedraw_ = false;
  return value;
}

void MenuApp::render(bool fullRedraw) {
  renderer_.render({items_, selectedIndex_}, fullRedraw);
}

AppId MenuApp::selectedAppId() const {
  if (items_.empty() || selectedIndex_ >= items_.size()) return AppId::MENU;
  return items_[selectedIndex_].id;
}

AppManager::AppManager(MenuApp& menu, std::initializer_list<IApp*> apps)
    : menu_(menu), apps_(apps) {}

bool AppManager::begin(AppId startupApp) {
  if (active_) return active_->id() == startupApp;
  return switchTo(startupApp);
}

void AppManager::onInput(InputEvent event) {
  if (!active_ || event == InputEvent::NONE) return;

  if (active_->id() == AppId::MENU) {
    if (event == InputEvent::NEXT_LONG) {
      const AppId selected = menu_.selectedAppId();
      if (selected != AppId::MENU) switchTo(selected);
      return;
    }
    if (event == InputEvent::PREV_LONG) return;
    active_->onButton(event);
    return;
  }

  if (event == InputEvent::PREV_LONG) {
    switchTo(AppId::MENU);
    return;
  }
  if (event == InputEvent::NEXT_LONG) return;
  active_->onButton(event);
}

void AppManager::tick(uint32_t nowMs) {
  if (active_) active_->tick(nowMs);
}

void AppManager::render() {
  if (!active_ || !active_->takeDirtyFlag()) return;
  const bool fullRedraw = active_->takeFullRedrawFlag();
  active_->render(fullRedraw);
}

AppId AppManager::activeAppId() const {
  return active_ ? active_->id() : AppId::MENU;
}

IApp* AppManager::findApp(AppId id) const {
  if (id == AppId::MENU) return const_cast<MenuApp*>(&menu_);
  for (IApp* app : apps_) {
    if (app && app->id() == id) return app;
  }
  return nullptr;
}

bool AppManager::switchTo(AppId id) {
  IApp* target = findApp(id);
  if (!target) return false;
  if (target == active_) return true;
  if (active_) active_->onExit();
  active_ = target;
  active_->onEnter();
  return true;
}
