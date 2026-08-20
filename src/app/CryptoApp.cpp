#include "CryptoApp.h"

bool CryptoApp::begin() {
  if (initialized_) return true;
  controller_.begin();
  controller_.setWifiOnline(device_.wifiConnected());
  screen_.begin(device_.display(), device_.unicodeFont());
  initialized_ = true;
  return true;
}

void CryptoApp::onEnter() {
  active_ = true;
  controller_.setActive(true);
  forceDirty_ = true;
  forceFullRedraw_ = true;
}
void CryptoApp::onExit() { active_ = false; controller_.setActive(false); }
void CryptoApp::onButton(InputEvent) {}
void CryptoApp::tick(uint32_t nowMs) {
  if (!initialized_ || !active_) return;
  controller_.setWifiOnline(device_.wifiConnected());
  controller_.tick(nowMs);
}
bool CryptoApp::takeDirtyFlag() {
  const bool dirty = forceDirty_ || controller_.takeDirtyFlag();
  forceDirty_ = false;
  return dirty;
}
bool CryptoApp::takeFullRedrawFlag() {
  const bool full = forceFullRedraw_ || controller_.takeFullRedrawFlag();
  forceFullRedraw_ = false;
  return full;
}
void CryptoApp::render(bool fullRedraw) {
  if (initialized_ && active_) screen_.render(controller_.viewModel(), fullRedraw);
}
