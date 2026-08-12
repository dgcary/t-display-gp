#include "DeviceLayer.h"

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "build_config.h"

namespace {
constexpr time_t MIN_SYNCED_EPOCH = 1704067200;  // 2024-01-01 UTC
}

DeviceLayer::DeviceLayer()
    : previousButton_(BuildConfig::BUTTON_DEBOUNCE_MS),
      nextButton_(BuildConfig::BUTTON_DEBOUNCE_MS) {}

void DeviceLayer::begin() {
  // T-Display-S3 display power must be asserted before initializing the panel.
  pinMode(BuildConfig::PIN_POWER, OUTPUT);
  digitalWrite(BuildConfig::PIN_POWER, HIGH);

  pinMode(BuildConfig::PIN_BUTTON_PREV, INPUT_PULLUP);
  pinMode(BuildConfig::PIN_BUTTON_NEXT, INPUT_PULLUP);

  tft_.init();
  tft_.setRotation(0);  // 170 x 320 portrait
  tft_.fillScreen(TFT_BLACK);

  unicodeFont_.begin(tft_);
  unicodeFont_.setFontMode(1);
  unicodeFont_.setFontDirection(0);
  unicodeFont_.setForegroundColor(TFT_WHITE);
  unicodeFont_.setBackgroundColor(TFT_BLACK);

  if (WiFi.status() == WL_CONNECTED) {
    configTzTime("CST-8", "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
  }
  drawSmokeScreen();
}

ButtonEvent DeviceLayer::pollButtons(uint32_t nowMs) {
  if (previousButton_.update(digitalRead(BuildConfig::PIN_BUTTON_PREV) != LOW, nowMs)) {
    return ButtonEvent::PREVIOUS;
  }
  if (nextButton_.update(digitalRead(BuildConfig::PIN_BUTTON_NEXT) != LOW, nowMs)) {
    return ButtonEvent::NEXT;
  }
  return ButtonEvent::NONE;
}

bool DeviceLayer::wifiConnected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool DeviceLayer::timeSynchronized() const {
  time_t now = 0;
  time(&now);
  return now >= MIN_SYNCED_EPOCH;
}

LocalDateTime DeviceLayer::localDateTime() const {
  LocalDateTime result{};
  time_t now = 0;
  time(&now);
  if (now < MIN_SYNCED_EPOCH) return result;

  tm local{};
  localtime_r(&now, &local);
  result.year = local.tm_year + 1900;
  result.month = local.tm_mon + 1;
  result.day = local.tm_mday;
  result.hour = local.tm_hour;
  result.minute = local.tm_min;
  result.second = local.tm_sec;
  result.dayOfWeek = local.tm_wday;
  return result;
}

void DeviceLayer::drawSmokeScreen() {
  tft_.fillScreen(TFT_BLACK);
  unicodeFont_.setFont(u8g2_font_wqy12_t_gb2312);
  unicodeFont_.setForegroundColor(TFT_WHITE);
  unicodeFont_.setCursor(10, 38);
  unicodeFont_.print("T-Display GP");
  unicodeFont_.setCursor(10, 72);
  unicodeFont_.print("屏幕 OK");
  unicodeFont_.setCursor(10, 106);
  unicodeFont_.print("BTN0 / BTN14");
  unicodeFont_.setCursor(10, 140);
  unicodeFont_.print(wifiConnected() ? "Wi-Fi: 已连接" : "Wi-Fi: 未连接");
}
