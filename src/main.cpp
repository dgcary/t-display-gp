#include <Arduino.h>
#include <time.h>

#include "AppConfig.h"
#include "app/StockController.h"
#include "device/ConfigStore.h"
#include "device/DeviceLayer.h"
#include "network/MarketDataWorker.h"
#include "network/ProvisioningService.h"
#include "ui/StockScreen.h"

namespace {
AppConfig appConfig;
ConfigStore configStore;
ProvisioningService provisioning;
DeviceLayer device;
MarketDataWorker dataWorker;
StockController controller(dataWorker);
StockScreen screen;
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

  // Load once for the documented lifecycle. ensureConnected() independently
  // validates persistent app configuration and forces the captive portal when
  // it is missing/invalid, even if Wi-Fi credentials already exist.
  configStore.load(appConfig);
  Serial.println("[boot] provisioning start");
  if (!provisioning.ensureConnected(appConfig)) {
    Serial.println("Provisioning failed; restarting");
    delay(1000);
    ESP.restart();
    return;
  }

  Serial.println("[boot] provisioning complete; starting application services");
  startChinaTimeSync();
  provisioning.beginWebPortal(appConfig);

  if (!dataWorker.begin()) {
    Serial.println("Market-data worker failed to start");
    return;
  }

  controller.begin(appConfig);
  controller.setWifiOnline(device.wifiConnected());
  screen.begin(device.display(), device.unicodeFont());
  appReady = true;
  Serial.println("[boot] market loop ready");
}

void loop() {
  provisioning.process();
  if (!appReady) {
    delay(1);
    return;
  }

  const uint32_t nowMs = millis();
  controller.setWifiOnline(device.wifiConnected());
  controller.onButton(device.pollButtons(nowMs));
  controller.consumeMarketResults();
  controller.tick(nowMs, device.localDateTime());

  if (controller.takeDirtyFlag()) {
    screen.render(controller.viewModel(), controller.takeFullRedrawFlag());
  }

  delay(1);
}
