#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <vector>

#include "BambuCloudClient.h"
#include "BambuConfig.h"
#include "BambuConfigStore.h"
#include "BambuSessionModel.h"
#include "BambuState.h"

class PubSubClient;
class WiFiClientSecure;

struct BambuMqttStatus {
  BambuSessionState session = BambuSessionState::UNCONFIGURED;
  bool mqttConnected = false;
  bool configured = false;
  bool passwordSet = false;
  bool tokenSet = false;
  int lastMqttRc = -1;
  uint32_t lastMessageMs = 0U;
  uint8_t reloginFailureCount = 0U;
};

class BambuMqttService {
 public:
  bool begin(BambuConfig& config, BambuConfigStore& store, BambuCloudClient& cloud);
  BambuState snapshot() const;
  BambuMqttStatus status() const;
  std::vector<BambuCloudDevice> discoveredPrinters() const;

 private:
  static void taskThunk(void* arg);
  static void mqttCallbackThunk(char* topic, uint8_t* payload, unsigned int length);

  void taskLoop();
  void handleMessage(const char* topic, const uint8_t* payload, unsigned int length);
  bool ensureCloudIdentity(uint32_t nowMs);
  bool discoverPrinterIfNeeded();
  bool connectMqtt(uint32_t nowMs);
  bool performRelogin(uint32_t nowMs);
  void disconnectMqtt();
  void setConnectivity(bool connected);
  void setSession(BambuSessionState state, int mqttRc = -1);
  BambuConfig configCopy() const;
  bool persistConfig(const BambuConfig& config);

  BambuConfig* externalConfig_ = nullptr;
  BambuConfigStore* store_ = nullptr;
  BambuCloudClient* cloud_ = nullptr;
  WiFiClientSecure* tls_ = nullptr;
  PubSubClient* mqtt_ = nullptr;
  TaskHandle_t task_ = nullptr;
  mutable SemaphoreHandle_t mutex_ = nullptr;
  BambuConfig config_;
  BambuState state_;
  BambuMqttStatus status_;
  std::vector<BambuCloudDevice> discoveredPrinters_;
  BambuSessionModel sessionModel_;
  uint32_t lastMqttAttemptMs_ = 0U;
  bool mqttAttempted_ = false;
  uint32_t pushallSequence_ = 1U;

  static BambuMqttService* activeInstance_;
};
