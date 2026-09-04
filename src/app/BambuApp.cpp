#include "BambuApp.h"

bool BambuApp::begin() {
  if (initialized_) return true;
  screen_.begin(device_.display(), device_.unicodeFont());
  initialized_ = true;
  return true;
}

void BambuApp::onEnter() {
  active_ = true;
  hasRefresh_ = false;
  dirty_ = true;
  fullRedraw_ = true;
}

void BambuApp::onExit() {
  // MQTT is a device-level background service. Leaving this app only stops UI
  // refreshes; it never connects, disconnects, or mutates the service session.
  active_ = false;
}

void BambuApp::onButton(InputEvent) {
  // V1 is intentionally read-only. Global long-press navigation is owned by
  // AppManager and there are no printer-control short-press actions.
}

void BambuApp::tick(uint32_t nowMs) {
  if (!initialized_ || !active_) return;
  if (!hasRefresh_ || static_cast<uint32_t>(nowMs - lastRefreshMs_) >= 500U) {
    model_.state = service_.snapshot();
    model_.service = service_.status();
    model_.printerName = config_.printerName;
    lastRefreshMs_ = nowMs;
    hasRefresh_ = true;
    dirty_ = true;
  }
}

bool BambuApp::takeDirtyFlag() {
  const bool value = dirty_;
  dirty_ = false;
  return value;
}

bool BambuApp::takeFullRedrawFlag() {
  const bool value = fullRedraw_;
  fullRedraw_ = false;
  return value;
}

void BambuApp::render(bool fullRedraw) {
  if (!initialized_ || !active_) return;
  screen_.render(model_, fullRedraw);
}
