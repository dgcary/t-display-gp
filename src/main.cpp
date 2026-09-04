#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>

#include "AppConfig.h"
#include "AppDataWorker.h"
#include "AppShell.h"
#include "ConfigStore.h"
#include "DeviceInfoApp.h"
#include "DeviceLayer.h"
#include "HomeAssistantApp.h"
#include "HomeAssistantConfig.h"
#include "HomeAssistantConfigPortal.h"
#include "HomeAssistantConfigStore.h"
#include "MenuScreen.h"
#include "NetworkArbiter.h"
#include "ProvisioningService.h"
#include "StockApp.h"
#include "WeatherApp.h"

namespace {
AppConfig appConfig;
HomeAssistantConfig homeAssistantConfig;
ConfigStore configStore;
HomeAssistantConfigStore homeAssistantConfigStore;
ProvisioningService provisioning;
HomeAssistantConfigPortal homeAssistantConfigPortal;
DeviceLayer device;
AppDataWorker appDataWorker;
MenuScreen menuScreen;
StockApp stockApp(device);
WeatherApp weatherApp(device, appDataWorker);
HomeAssistantApp homeAssistantApp(device, appDataWorker);
DeviceInfoApp deviceInfoApp(device);
MenuApp menuApp({{AppId::STOCK, "股票"},
                 {AppId::WEATHER, "天气"},
                 {AppId::HOME_ASSISTANT, "智能家居"},
                 {AppId::DEVICE_INFO, "设备信息"}},
                menuScreen);
AppManager appManager(menuApp, {&stockApp, &weatherApp, &homeAssistantApp, &deviceInfoApp});
bool appReady = false;
uint32_t nextResourceLogMs = 0;

void startChinaTimeSync() { configTzTime("CST-8", "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov"); }

const char* appName(AppId id) {
  switch (id) {
    case AppId::MENU: return "MENU";
    case AppId::STOCK: return "STOCK";
    case AppId::WEATHER: return "WEATHER";
    case AppId::HOME_ASSISTANT: return "HOME_ASSISTANT";
    case AppId::DEVICE_INFO: return "DEVICE_INFO";
  }
  return "UNKNOWN";
}

void logResourceSnapshot(uint32_t nowMs) {
  Serial.printf("[sys] app=%s heap_free=%u heap_min=%u psram_free=%u psram_total=%u main_stack_hwm=%u\n",
                appName(appManager.activeAppId()), static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMinFreeHeap()), static_cast<unsigned>(ESP.getFreePsram()),
                static_cast<unsigned>(ESP.getPsramSize()),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  nextResourceLogMs = nowMs + 60000U;
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("[boot] T-Display GP starting");
  device.begin();
  configStore.load(appConfig);
  homeAssistantConfigStore.load(homeAssistantConfig);
  Serial.println("[boot] provisioning start");
  if (!provisioning.ensureConnected(appConfig)) {
    Serial.println("Provisioning failed; restarting");
    delay(1000);
    ESP.restart();
    return;
  }
  Serial.println("[boot] provisioning complete; starting shared services");
  startChinaTimeSync();
  provisioning.beginWebPortal(appConfig);
  homeAssistantConfigPortal.begin(homeAssistantConfig);
  if (!sharedNetworkArbiter().begin()) {
    Serial.println("Network arbiter failed to start");
    return;
  }
  if (!appDataWorker.begin()) {
    Serial.println("App-data worker failed to start");
    return;
  }
  menuScreen.begin(device.display(), device.unicodeFont());
  if (!stockApp.begin(appConfig)) { Serial.println("Stock app failed to start"); return; }
  if (!weatherApp.begin(appConfig)) { Serial.println("Weather app failed to start"); return; }
  if (!homeAssistantApp.begin(homeAssistantConfig)) { Serial.println("Home Assistant app failed to start"); return; }
  if (!deviceInfoApp.begin()) { Serial.println("Device info app failed to start"); return; }
  if (!appManager.begin(AppId::STOCK)) { Serial.println("App manager failed to start"); return; }
  appReady = true;
  Serial.println("[boot] multi-app loop ready");
  logResourceSnapshot(millis());
}

void loop() {
  provisioning.process();
  homeAssistantConfigPortal.process();
  if (!appReady) { delay(1); return; }
  const uint32_t nowMs = millis();
  appManager.onInput(device.pollButtons(nowMs));
  appManager.tick(nowMs);
  appManager.render();
  if (static_cast<int32_t>(nowMs - nextResourceLogMs) >= 0) logResourceSnapshot(nowMs);
  delay(1);
}
