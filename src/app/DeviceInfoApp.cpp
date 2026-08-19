#include "DeviceInfoApp.h"

#include <Arduino.h>
#include <WiFi.h>

#include <cstdio>

bool DeviceInfoApp::begin() {
  if (initialized_) return true;
  screen_.begin(device_.display(), device_.unicodeFont());
  initialized_ = true;
  return true;
}

void DeviceInfoApp::onEnter() {
  active_ = true;
  hasRefresh_ = false;
  dirty_ = true;
  fullRedraw_ = true;
}

void DeviceInfoApp::onExit() {
  active_ = false;
}

void DeviceInfoApp::onButton(InputEvent) {
  // Device info has no short-press actions. Global long-press handling remains
  // owned by AppManager.
}

void DeviceInfoApp::refreshModel(uint32_t nowMs) {
  model_.wifiOnline = device_.wifiConnected();
  model_.ip = WiFi.localIP().toString().c_str();
  model_.ssid = WiFi.SSID().c_str();
  model_.mac = WiFi.macAddress().c_str();
  model_.rssi = model_.wifiOnline ? WiFi.RSSI() : 0;
  model_.uptimeMs = nowMs;
  model_.heapFree = ESP.getFreeHeap();
  model_.heapMin = ESP.getMinFreeHeap();
  model_.psramFree = ESP.getFreePsram();
  model_.psramTotal = ESP.getPsramSize();

  const LocalDateTime local = device_.localDateTime();
  if (local.year > 0) {
    char text[24] = {};
    std::snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d:%02d",
                  local.year, local.month, local.day, local.hour, local.minute, local.second);
    model_.localTime = text;
  } else {
    model_.localTime = "--";
  }

  lastRefreshMs_ = nowMs;
  hasRefresh_ = true;
  dirty_ = true;
}

void DeviceInfoApp::tick(uint32_t nowMs) {
  if (!initialized_ || !active_) return;
  if (!hasRefresh_ || static_cast<uint32_t>(nowMs - lastRefreshMs_) >= 1000U) {
    refreshModel(nowMs);
  }
}

bool DeviceInfoApp::takeDirtyFlag() {
  const bool value = dirty_;
  dirty_ = false;
  return value;
}

bool DeviceInfoApp::takeFullRedrawFlag() {
  const bool value = fullRedraw_;
  fullRedraw_ = false;
  return value;
}

void DeviceInfoApp::render(bool fullRedraw) {
  if (!initialized_ || !active_) return;
  screen_.render(model_, fullRedraw);
}
