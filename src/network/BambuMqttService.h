#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>

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
  bool tokenSet = false;
  int lastMqttRc = -1;
  uint32_t lastMessageMs = 0U;
};

class BambuMqttService {
 public:
  bool begin(const BambuConfig& config, BambuConfigStore& store);
  BambuState snapshot() const;
  BambuMqttStatus status() const;
  BambuConfig configSnapshot() const;
  bool replaceConfig(const BambuConfig& config);
  bool cycleActivePrinter(int direction);

 private:
  static void taskThunk(void* arg);
  static void mqttCallbackThunk(char* topic, uint8_t* payload, unsigned int length);

  void taskLoop();
  void handleMessage(const char* topic, const uint8_t* payload, unsigned int length);
  bool connectMqtt(uint32_t nowMs);
  void disconnectMqtt();
  void setConnectivity(bool connected);
  void setSession(BambuSessionState state, int mqttRc = -1);
  BambuConfig configCopy(uint32_t* externalRevision = nullptr) const;
  void applyConfigLocked(const BambuConfig& config);

  BambuConfigStore* store_ = nullptr;
  WiFiClientSecure* tls_ = nullptr;
  PubSubClient* mqtt_ = nullptr;
  TaskHandle_t task_ = nullptr;
  mutable SemaphoreHandle_t mutex_ = nullptr;
  BambuConfig config_;
  BambuState state_;
  BambuMqttStatus status_;
  uint32_t externalConfigRevision_ = 0U;
  uint32_t observedExternalConfigRevision_ = 0U;
  uint32_t lastMqttAttemptMs_ = 0U;
  bool mqttAttempted_ = false;
  bool tokenRejected_ = false;
  uint32_t pushallSequence_ = 1U;

  static BambuMqttService* activeInstance_;
};
