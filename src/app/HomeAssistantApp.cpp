#include "HomeAssistantApp.h"

#include <WiFi.h>

bool HomeAssistantApp::begin(const HomeAssistantConfig& config) {
  if (initialized_) return true;
  controller_.begin(config);
  controller_.setWifiOnline(device_.wifiConnected());
  screen_.begin(device_.display(), device_.unicodeFont());
  initialized_ = true;
  return true;
}
void HomeAssistantApp::onEnter() {
  active_ = true;
  controller_.setActive(true);
  forceDirty_ = true;
  forceFullRedraw_ = true;
}
void HomeAssistantApp::onExit() { active_ = false; controller_.setActive(false); }
void HomeAssistantApp::onButton(InputEvent) {}
void HomeAssistantApp::tick(uint32_t nowMs) {
  if (!initialized_ || !active_) return;
  controller_.setWifiOnline(device_.wifiConnected());
  controller_.tick(nowMs);
}
bool HomeAssistantApp::takeDirtyFlag() {
  const bool dirty = forceDirty_ || controller_.takeDirtyFlag();
  forceDirty_ = false;
  return dirty;
}
bool HomeAssistantApp::takeFullRedrawFlag() {
  const bool full = forceFullRedraw_ || controller_.takeFullRedrawFlag();
  forceFullRedraw_ = false;
  return full;
}
void HomeAssistantApp::render(bool fullRedraw) {
  if (!initialized_ || !active_) return;
  const std::string setupUrl = "http://" + std::string(WiFi.localIP().toString().c_str()) + ":8081/";
  screen_.render(controller_.viewModel(), setupUrl, fullRedraw);
}
