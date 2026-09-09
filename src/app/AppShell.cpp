#include "AppShell.h"

#include <utility>

MenuApp::MenuApp(std::vector<AppDescriptor> items, IMenuRenderer& renderer)
    : items_(std::move(items)), renderer_(renderer) {}

void MenuApp::onEnter() {
  dirty_ = true;
  fullRedraw_ = true;
}

void MenuApp::onExit() {}

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

AppManager::AppManager(std::initializer_list<IApp*> apps) : apps_(apps) {}

bool AppManager::begin() {
  if (active_) return true;
  for (IApp* app : apps_) {
    if (app && app->pageCount() > 0U) return activate(app, 0U);
  }
  return false;
}

void AppManager::onInput(InputEvent event) {
  if (!active_ || event == InputEvent::NONE) return;
  if (event == InputEvent::PREV_SHORT) {
    navigate(-1);
  } else if (event == InputEvent::NEXT_SHORT) {
    navigate(1);
  }
  // Long presses are intentionally reserved/no-op in direct-navigation mode.
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

size_t AppManager::activePageIndex() const {
  return active_ ? active_->selectedPage() : 0U;
}

IApp* AppManager::findApp(AppId id) const {
  for (IApp* app : apps_) {
    if (app && app->id() == id) return app;
  }
  return nullptr;
}

bool AppManager::activate(IApp* app, size_t pageIndex) {
  if (!app || pageIndex >= app->pageCount() || !app->selectPage(pageIndex)) return false;
  if (app == active_) return true;
  if (active_) active_->onExit();
  active_ = app;
  active_->onEnter();
  return true;
}

bool AppManager::navigate(int direction) {
  if (!active_ || direction == 0 || apps_.empty()) return false;

  const size_t pageCount = active_->pageCount();
  const size_t pageIndex = active_->selectedPage();
  if (pageCount > 0U && pageIndex < pageCount) {
    if (direction > 0 && pageIndex + 1U < pageCount) {
      return active_->selectPage(pageIndex + 1U);
    }
    if (direction < 0 && pageIndex > 0U) {
      return active_->selectPage(pageIndex - 1U);
    }
  }

  size_t activeIndex = apps_.size();
  for (size_t i = 0; i < apps_.size(); ++i) {
    if (apps_[i] == active_) {
      activeIndex = i;
      break;
    }
  }
  if (activeIndex >= apps_.size()) return false;

  for (size_t step = 1U; step <= apps_.size(); ++step) {
    size_t candidateIndex = 0U;
    if (direction > 0) {
      candidateIndex = (activeIndex + step) % apps_.size();
    } else {
      candidateIndex = (activeIndex + apps_.size() - (step % apps_.size())) % apps_.size();
    }

    IApp* candidate = apps_[candidateIndex];
    if (!candidate) continue;
    const size_t candidatePages = candidate->pageCount();
    if (candidatePages == 0U) continue;
    const size_t targetPage = direction > 0 ? 0U : candidatePages - 1U;
    return activate(candidate, targetPage);
  }
  return false;
}
