#include <Arduino.h>
#include <time.h>

#include "AppConfig.h"
#include "AppDataWorker.h"
#include "AppShell.h"
#include "ConfigStore.h"
#include "DeviceLayer.h"
#include "MenuScreen.h"
#include "NetworkArbiter.h"
#include "ProvisioningService.h"
#include "StockApp.h"
#include "WeatherApp.h"

namespace {
AppConfig appConfig;
ConfigStore configStore;
ProvisioningService provisioning;
DeviceLayer device;
AppDataWorker appDataWorker;
MenuScreen menuScreen;
StockApp stockApp(device);
WeatherApp weatherApp(device, appDataWorker);
MenuApp menuApp({{AppId::STOCK, "股票"}, {AppId::WEATHER, "天气"}}, menuScreen);
AppManager appManager(menuApp, {&stockApp, &weatherApp});
bool appReady = false;

void startChinaTimeSync() {
  configTzTime("CST-8", "ntp.aliyun.com", "pool.ntp.org", "time.nist.gov");
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("[boot] T-Display GP starting");

  // Bring up the panel first so provisioning has visible feedback even on a
  // factory-erased device. DeviceLayer intentionally does not wait for NTP.
  device.begin();

  configStore.load(appConfig);
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

  if (!sharedNetworkArbiter().begin()) {
    Serial.println("Network arbiter failed to start");
    return;
  }
  if (!appDataWorker.begin()) {
    Serial.println("App-data worker failed to start");
    return;
  }

  menuScreen.begin(device.display(), device.unicodeFont());
  if (!stockApp.begin(appConfig)) {
    Serial.println("Stock app failed to start");
    return;
  }
  if (!weatherApp.begin(appConfig)) {
    Serial.println("Weather app failed to start");
    return;
  }
  if (!appManager.begin(AppId::STOCK)) {
    Serial.println("App manager failed to start");
    return;
  }

  appReady = true;
  Serial.println("[boot] multi-app loop ready");
}

void loop() {
  provisioning.process();
  if (!appReady) {
    delay(1);
    return;
  }

  const uint32_t nowMs = millis();
  appManager.onInput(device.pollButtons(nowMs));
  appManager.tick(nowMs);
  appManager.render();

  delay(1);
}
