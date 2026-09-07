#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <string>
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
  bool verificationRequired = false;
  BambuVerificationType verificationType = BambuVerificationType::NONE;
  uint32_t verificationResendAfterMs = 0U;
  int lastMqttRc = -1;
  uint32_t lastMessageMs = 0U;
  uint8_t reloginFailureCount = 0U;
};

class BambuMqttService {
 public:
  bool begin(const BambuConfig& config, BambuConfigStore& store, BambuCloudClient& cloud);
  BambuState snapshot() const;
  BambuMqttStatus status() const;
  BambuConfig configSnapshot() const;
  bool replaceConfig(const BambuConfig& config);
  std::vector<BambuCloudDevice> discoveredPrinters() const;

  // Challenge material is RAM-only and owned by this service. The Portal never
  // stores a tfaKey or verification code and status only exposes typed booleans.
  bool setPendingVerification(const BambuCloudLoginResult& challenge);
  BambuCloudLoginResult submitVerificationCode(std::string code);
  BambuCloudError requestVerificationCode();

 private:
  enum class ConfigCommitResult {
    SAVED = 0,
    STALE,
    ERROR,
  };

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
  BambuConfig configCopy(uint32_t* externalRevision = nullptr) const;
  bool publishDiscoveredPrintersIfCurrent(const std::vector<BambuCloudDevice>& printers,
                                          uint32_t expectedExternalRevision);
  ConfigCommitResult persistConfig(const BambuConfig& config,
                                   uint32_t expectedExternalRevision);
  bool setPendingVerification(const BambuCloudLoginResult& challenge,
                              uint32_t expectedExternalRevision);
  bool hasPendingVerification() const;
  void clearPendingVerificationLocked();
  ConfigCommitResult completeVerificationConfig(const BambuConfig& config,
                                                uint32_t expectedExternalRevision);

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
  BambuVerificationType pendingVerificationType_ = BambuVerificationType::NONE;
  std::string pendingTfaKey_;
  uint32_t pendingVerificationRevision_ = 0U;
  uint32_t verificationResendAnchorMs_ = 0U;
  bool verificationResendAnchorSet_ = false;
  uint32_t externalConfigRevision_ = 0U;
  uint32_t observedExternalConfigRevision_ = 0U;
  uint32_t lastMqttAttemptMs_ = 0U;
  bool mqttAttempted_ = false;
  uint32_t pushallSequence_ = 1U;

  static BambuMqttService* activeInstance_;
};
