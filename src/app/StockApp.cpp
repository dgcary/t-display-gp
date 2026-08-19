#include "StockApp.h"

bool StockApp::begin(const AppConfig& config) {
  if (initialized_) return true;
  if (!worker_.begin()) return false;
  worker_.setPaused(true);
  controller_.begin(config);
  controller_.setWifiOnline(device_.wifiConnected());
  screen_.begin(device_.display(), device_.unicodeFont());
  initialized_ = true;
  return true;
}

void StockApp::onEnter() {
  active_ = true;
  worker_.setPaused(false);
  forceDirty_ = true;
  forceFullRedraw_ = true;
}

void StockApp::onExit() {
  worker_.setPaused(true);
  active_ = false;
}

void StockApp::onButton(InputEvent event) {
  if (!initialized_ || !active_) return;
  if (event == InputEvent::PREV_SHORT) {
    controller_.onButton(ButtonEvent::PREVIOUS);
  } else if (event == InputEvent::NEXT_SHORT) {
    controller_.onButton(ButtonEvent::NEXT);
  }
}

void StockApp::tick(uint32_t nowMs) {
  if (!initialized_ || !active_) return;
  controller_.setWifiOnline(device_.wifiConnected());
  controller_.consumeMarketResults();
  controller_.tick(nowMs, device_.localDateTime());
}

bool StockApp::takeDirtyFlag() {
  const bool dirty = forceDirty_ || controller_.takeDirtyFlag();
  forceDirty_ = false;
  return dirty;
}

bool StockApp::takeFullRedrawFlag() {
  const bool full = forceFullRedraw_ || controller_.takeFullRedrawFlag();
  forceFullRedraw_ = false;
  return full;
}

void StockApp::render(bool fullRedraw) {
  if (!initialized_ || !active_) return;
  screen_.render(controller_.viewModel(), fullRedraw);
}
