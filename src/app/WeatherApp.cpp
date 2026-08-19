#include "WeatherApp.h"

bool WeatherApp::begin(const AppConfig& config) {
  if (initialized_) return true;
  controller_.begin(config);
  controller_.setWifiOnline(device_.wifiConnected());
  screen_.begin(device_.display(), device_.unicodeFont());
  initialized_ = true;
  return true;
}

void WeatherApp::onEnter() {
  active_ = true;
  controller_.setActive(true);
  forceDirty_ = true;
  forceFullRedraw_ = true;
}

void WeatherApp::onExit() {
  active_ = false;
  controller_.setActive(false);
}

void WeatherApp::onButton(InputEvent) {
  // Weather V1 has no short-press sub-navigation. Global long-press handling
  // remains owned by AppManager.
}

void WeatherApp::tick(uint32_t nowMs) {
  if (!initialized_ || !active_) return;
  controller_.setWifiOnline(device_.wifiConnected());
  controller_.tick(nowMs);
}

bool WeatherApp::takeDirtyFlag() {
  const bool dirty = forceDirty_ || controller_.takeDirtyFlag();
  forceDirty_ = false;
  return dirty;
}

bool WeatherApp::takeFullRedrawFlag() {
  const bool full = forceFullRedraw_ || controller_.takeFullRedrawFlag();
  forceFullRedraw_ = false;
  return full;
}

void WeatherApp::render(bool fullRedraw) {
  if (!initialized_ || !active_) return;
  screen_.render(controller_.viewModel(), fullRedraw);
}
