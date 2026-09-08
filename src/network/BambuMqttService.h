#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <array>
#include <cstddef>
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
  void process(uint32_t nowMs);
  BambuState snapshot() const;
  BambuMqttStatus status() const;
  BambuConfig configSnapshot() const;
  bool replaceConfig(const BambuConfig& config);
  bool cycleActivePrinter(int direction);

 private:
  struct MqttConn {
    WiFiClientSecure* tls = nullptr;
    PubSubClient* mqtt = nullptr;
    BambuMqttStatus status{};
    uint32_t lastMqttAttemptMs = 0U;
    uint32_t connectTimeMs = 0U;
    bool mqttAttempted = false;
    bool tokenRejected = false;
    bool initialPushallPending = false;
    uint16_t consecutiveFails = 0U;
    uint32_t pushallSequence = 1U;
  };

  static void mqttCallbackThunk(char* topic, uint8_t* payload, unsigned int length);

  void handleMessage(const char* topic, const uint8_t* payload, unsigned int length);
  size_t findSlotForTopic(const char* topic) const;
  bool connectSlot(size_t slot, uint32_t nowMs);
  bool publishInitialPushall(size_t slot);
  uint32_t reconnectIntervalMs(const MqttConn& conn) const;
  void disconnectSlot(size_t slot);
  void disconnectAll();
  void resetSlotRuntime(size_t slot, const BambuConfig& config);
  void setSlotConnectivity(size_t slot, bool connected);
  void setSlotSession(size_t slot, BambuSessionState state, int mqttRc = -1);
  BambuConfig configCopy(uint32_t* externalRevision = nullptr) const;
  void applyConfigLocked(const BambuConfig& config);

  BambuConfigStore* store_ = nullptr;
  mutable SemaphoreHandle_t mutex_ = nullptr;
  BambuConfig config_;
  std::array<MqttConn, BambuConfigLimits::PRINTER_COUNT> conns_{};
  std::array<BambuState, BambuConfigLimits::PRINTER_COUNT> states_{};
  uint32_t externalConfigRevision_ = 0U;
  uint32_t observedExternalConfigRevision_ = 0U;

  static BambuMqttService* activeInstance_;
};
