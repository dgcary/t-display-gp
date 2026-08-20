#include "NixieClockApp.h"

bool NixieClockApp::begin() {
  if (initialized_) return true;
  screen_.begin(device_.display(), device_.unicodeFont());
  initialized_ = true;
  return true;
}

void NixieClockApp::onEnter() {
  active_ = true;
  hasTimeRefresh_ = false;
  dirty_ = true;
  fullRedraw_ = true;
}

void NixieClockApp::onExit() {
  active_ = false;
}

void NixieClockApp::onButton(InputEvent) {
  // Nixie Clock has no short-press action. Global long-press handling remains
  // owned by AppManager so GPIO0-long always returns to the main menu.
}

void NixieClockApp::refreshTime(uint32_t nowMs) {
  model_ = NixieClockModel::fromLocalDateTime(device_.localDateTime(), nowMs);
  lastTimeRefreshMs_ = nowMs;
  hasTimeRefresh_ = true;
  dirty_ = true;
}

void NixieClockApp::tick(uint32_t nowMs) {
  if (!initialized_ || !active_) return;

  if (!hasTimeRefresh_ || static_cast<uint32_t>(nowMs - lastTimeRefreshMs_) >= 1000U) {
    refreshTime(nowMs);
    return;
  }

  const bool colon = NixieClockModel::colonVisible(nowMs);
  if (model_.colonVisible != colon) {
    model_.colonVisible = colon;
    dirty_ = true;
  }
}

bool NixieClockApp::takeDirtyFlag() {
  const bool value = dirty_;
  dirty_ = false;
  return value;
}

bool NixieClockApp::takeFullRedrawFlag() {
  const bool value = fullRedraw_;
  fullRedraw_ = false;
  return value;
}

void NixieClockApp::render(bool fullRedraw) {
  if (!initialized_ || !active_) return;
  screen_.render(model_, fullRedraw);
}
