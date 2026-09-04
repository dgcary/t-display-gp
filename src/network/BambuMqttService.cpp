#include "BambuMqttService.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>

#include <cstdio>
#include <new>
#include <string>
#include <utility>

#include "BambuCloudProtocol.h"
#include "NetworkArbiter.h"
#include "build_config.h"

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

namespace {
constexpr uint16_t BAMBU_MQTT_PORT = 8883U;
constexpr uint16_t BAMBU_MQTT_KEEPALIVE_SEC = 30U;
constexpr uint32_t BAMBU_MQTT_RECONNECT_MS = 30000U;
constexpr uint32_t BAMBU_TASK_SLEEP_MS = 50U;
constexpr uint32_t BAMBU_WIFI_WAIT_MS = 500U;

class NetworkRequestGuard {
 public:
  explicit NetworkRequestGuard(NetworkArbiter& arbiter)
      : arbiter_(arbiter), locked_(arbiter_.lock()) {}
  ~NetworkRequestGuard() {
    if (locked_) arbiter_.unlock();
  }
  bool locked() const { return locked_; }

 private:
  NetworkArbiter& arbiter_;
  bool locked_ = false;
};

BambuReloginFailure reloginFailureFor(BambuCloudError error) {
  if (error == BambuCloudError::TWO_FACTOR_REQUIRED) {
    return BambuReloginFailure::TWO_FACTOR_REQUIRED;
  }
  if (error == BambuCloudError::INVALID_CREDENTIALS ||
      error == BambuCloudError::HTTP_STATUS ||
      error == BambuCloudError::MALFORMED ||
      error == BambuCloudError::USER_ID_UNAVAILABLE) {
    return BambuReloginFailure::INVALID_CREDENTIALS;
  }
  return BambuReloginFailure::NETWORK;
}

bool elapsed(uint32_t nowMs, uint32_t sinceMs, uint32_t intervalMs) {
  return static_cast<uint32_t>(nowMs - sinceMs) >= intervalMs;
}
}  // namespace

BambuMqttService* BambuMqttService::activeInstance_ = nullptr;

bool BambuMqttService::begin(BambuConfig& config,
                             BambuConfigStore& store,
                             BambuCloudClient& cloud) {
  if (task_ || activeInstance_) return false;

  mutex_ = xSemaphoreCreateMutex();
  if (!mutex_) return false;

  externalConfig_ = &config;
  store_ = &store;
  cloud_ = &cloud;
  config_ = config;
  status_.configured = config_.enabled && !config_.email.empty();
  status_.passwordSet = !config_.password.empty();
  status_.tokenSet = !config_.accessToken.empty();
  status_.session = config_.enabled ? BambuSessionState::MQTT_CONNECTING
                                    : BambuSessionState::DISABLED;
  sessionModel_.setAutomaticReloginAvailable(!config_.password.empty());
  activeInstance_ = this;

  const BaseType_t created = xTaskCreatePinnedToCore(
      taskThunk, "bambu-mqtt", 8192, this, 1, &task_, 0);
  if (created != pdPASS) {
    activeInstance_ = nullptr;
    task_ = nullptr;
    vSemaphoreDelete(mutex_);
    mutex_ = nullptr;
    return false;
  }
  return true;
}

BambuState BambuMqttService::snapshot() const {
  BambuState copy;
  if (!mutex_) return copy;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    copy = state_;
    xSemaphoreGive(mutex_);
  }
  return copy;
}

BambuMqttStatus BambuMqttService::status() const {
  BambuMqttStatus copy;
  if (!mutex_) return copy;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    copy = status_;
    xSemaphoreGive(mutex_);
  }
  return copy;
}

std::vector<BambuCloudDevice> BambuMqttService::discoveredPrinters() const {
  std::vector<BambuCloudDevice> copy;
  if (!mutex_) return copy;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    copy = discoveredPrinters_;
    xSemaphoreGive(mutex_);
  }
  return copy;
}

void BambuMqttService::taskThunk(void* arg) {
  static_cast<BambuMqttService*>(arg)->taskLoop();
}

void BambuMqttService::mqttCallbackThunk(char* topic,
                                         uint8_t* payload,
                                         unsigned int length) {
  if (activeInstance_) activeInstance_->handleMessage(topic, payload, length);
}

void BambuMqttService::taskLoop() {
  for (;;) {
    const uint32_t nowMs = millis();
    const BambuConfig config = configCopy();

    if (!config.enabled) {
      disconnectMqtt();
      setSession(BambuSessionState::DISABLED);
      vTaskDelay(pdMS_TO_TICKS(BAMBU_WIFI_WAIT_MS));
      continue;
    }

    if (WiFi.status() != WL_CONNECTED) {
      disconnectMqtt();
      setSession(BambuSessionState::NETWORK_ERROR);
      vTaskDelay(pdMS_TO_TICKS(BAMBU_WIFI_WAIT_MS));
      continue;
    }

    if (!ensureCloudIdentity(nowMs)) {
      vTaskDelay(pdMS_TO_TICKS(BAMBU_WIFI_WAIT_MS));
      continue;
    }

    if (!discoverPrinterIfNeeded()) {
      vTaskDelay(pdMS_TO_TICKS(BAMBU_WIFI_WAIT_MS));
      continue;
    }

    if (!mqtt_ || !mqtt_->connected()) {
      setConnectivity(false);
      if (!mqttAttempted_ || elapsed(nowMs, lastMqttAttemptMs_, BAMBU_MQTT_RECONNECT_MS)) {
        connectMqtt(nowMs);
      }
      vTaskDelay(pdMS_TO_TICKS(BAMBU_TASK_SLEEP_MS));
      continue;
    }

    if (!mqtt_->loop()) {
      setConnectivity(false);
    }
    vTaskDelay(pdMS_TO_TICKS(BAMBU_TASK_SLEEP_MS));
  }
}

void BambuMqttService::handleMessage(const char* topic,
                                     const uint8_t* payload,
                                     unsigned int length) {
  const BambuConfig config = configCopy();
  const std::string expected = bambuReportTopic(config.printerSerial);
  if (!topic || expected.empty() || expected != topic || !payload || length == 0U ||
      length > BambuStateLimits::REPORT_JSON) {
    return;
  }

  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return;
  const std::string_view json(reinterpret_cast<const char*>(payload), length);
  if (applyBambuReport(json, millis(), state_)) {
    state_.connected = true;
    status_.mqttConnected = true;
    status_.session = BambuSessionState::ONLINE;
    status_.lastMessageMs = state_.lastUpdateMs;
    status_.lastMqttRc = 0;
  }
  xSemaphoreGive(mutex_);
}

bool BambuMqttService::ensureCloudIdentity(uint32_t nowMs) {
  BambuConfig config = configCopy();
  sessionModel_.setAutomaticReloginAvailable(!config.password.empty());

  if (config.email.empty()) {
    setSession(BambuSessionState::UNCONFIGURED);
    return false;
  }

  if (config.accessToken.empty()) {
    if (config.password.empty()) {
      setSession(BambuSessionState::TOKEN_INVALID);
      return false;
    }
    sessionModel_.onMqttAuthFailure(nowMs);
  }

  if (sessionModel_.shouldRelogin(nowMs)) {
    return performRelogin(nowMs);
  }

  config = configCopy();
  if (config.accessToken.empty()) {
    setSession(sessionModel_.state());
    return false;
  }

  if (config.cloudUserId.empty()) {
    const BambuCloudUserIdResult user = cloud_->fetchUserId(config.accessToken, config.region);
    if (!user.ok()) {
      if (user.error == BambuCloudError::INVALID_CREDENTIALS ||
          user.error == BambuCloudError::HTTP_STATUS) {
        sessionModel_.onMqttAuthFailure(nowMs);
        setSession(sessionModel_.state());
      } else {
        setSession(BambuSessionState::NETWORK_ERROR);
      }
      return false;
    }
    config.cloudUserId = user.userId;
    if (!persistConfig(config)) {
      setSession(BambuSessionState::LOGIN_FAILED);
      return false;
    }
  }
  return true;
}

bool BambuMqttService::discoverPrinterIfNeeded() {
  BambuConfig config = configCopy();
  if (!config.printerSerial.empty()) return true;

  const BambuCloudPrintersResult found = cloud_->fetchPrinters(config.accessToken, config.region);
  if (!found.ok()) {
    setSession(found.error == BambuCloudError::NETWORK || found.error == BambuCloudError::TLS
                   ? BambuSessionState::NETWORK_ERROR
                   : BambuSessionState::LOGIN_FAILED);
    return false;
  }

  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    discoveredPrinters_ = found.printers;
    xSemaphoreGive(mutex_);
  }

  if (found.printers.empty()) {
    setSession(BambuSessionState::UNCONFIGURED);
    return false;
  }
  if (found.printers.size() != 1U) {
    setSession(BambuSessionState::PRINTER_SELECTION_REQUIRED);
    return false;
  }

  config.printerSerial = found.printers[0].serial;
  config.printerName = found.printers[0].name;
  if (!persistConfig(config)) {
    setSession(BambuSessionState::LOGIN_FAILED);
    return false;
  }
  return true;
}

bool BambuMqttService::connectMqtt(uint32_t nowMs) {
  lastMqttAttemptMs_ = nowMs;
  mqttAttempted_ = true;
  const BambuConfig config = configCopy();

  if (config.cloudUserId.empty() || config.accessToken.empty() || config.printerSerial.empty()) {
    setSession(BambuSessionState::UNCONFIGURED);
    return false;
  }

  disconnectMqtt();
  tls_ = new (std::nothrow) WiFiClientSecure();
  if (!tls_) {
    setSession(BambuSessionState::BUFFER_ERROR);
    return false;
  }
  tls_->setCACertBundle(rootca_crt_bundle_start);
  tls_->setHandshakeTimeout(BuildConfig::HTTP_TLS_HANDSHAKE_TIMEOUT_SEC);
  tls_->setTimeout(15);

  mqtt_ = new (std::nothrow) PubSubClient(*tls_);
  if (!mqtt_) {
    disconnectMqtt();
    setSession(BambuSessionState::BUFFER_ERROR);
    return false;
  }
  mqtt_->setServer(bambuBrokerForRegion(config.region), BAMBU_MQTT_PORT);
  mqtt_->setCallback(mqttCallbackThunk);
  mqtt_->setKeepAlive(BAMBU_MQTT_KEEPALIVE_SEC);
  if (!mqtt_->setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)) {
    disconnectMqtt();
    setSession(BambuSessionState::BUFFER_ERROR);
    return false;
  }

  setSession(BambuSessionState::MQTT_CONNECTING);
  NetworkRequestGuard guard(sharedNetworkArbiter());
  if (!guard.locked()) {
    disconnectMqtt();
    setSession(BambuSessionState::NETWORK_ERROR);
    return false;
  }

  char clientId[32];
  std::snprintf(clientId, sizeof(clientId), "tdgp_%08lx%04x",
                static_cast<unsigned long>(esp_random()),
                static_cast<unsigned>(esp_random() & 0xFFFFU));

  if (!mqtt_->connect(clientId, config.cloudUserId.c_str(), config.accessToken.c_str())) {
    const int rc = mqtt_->state();
    setSession((rc == 4 || rc == 5) ? BambuSessionState::TOKEN_INVALID
                                     : BambuSessionState::NETWORK_ERROR,
               rc);
    disconnectMqtt();
    if (rc == 4 || rc == 5) {
      sessionModel_.setAutomaticReloginAvailable(!config.password.empty());
      sessionModel_.onMqttAuthFailure(nowMs);
    }
    return false;
  }

  const std::string reportTopic = bambuReportTopic(config.printerSerial);
  if (reportTopic.empty() || !mqtt_->subscribe(reportTopic.c_str())) {
    setSession(BambuSessionState::NETWORK_ERROR, mqtt_->state());
    disconnectMqtt();
    return false;
  }

  const std::string requestTopic = "device/" + config.printerSerial + "/request";
  char request[144];
  std::snprintf(request, sizeof(request),
                "{\"pushing\":{\"sequence_id\":\"%lu\",\"command\":\"pushall\","
                "\"version\":1,\"push_target\":1}}",
                static_cast<unsigned long>(pushallSequence_++));
  mqtt_->publish(requestTopic.c_str(), request);

  setConnectivity(true);
  setSession(BambuSessionState::ONLINE, 0);
  return true;
}

bool BambuMqttService::performRelogin(uint32_t nowMs) {
  const BambuConfig config = configCopy();
  if (config.password.empty()) {
    setSession(BambuSessionState::TOKEN_INVALID);
    return false;
  }

  sessionModel_.onReloginStarted();
  setSession(BambuSessionState::RELOGIN_IN_PROGRESS);

  const BambuCloudLoginResult login = cloud_->login(config.email, config.password, config.region);
  if (!login.ok()) {
    const BambuReloginFailure reason = reloginFailureFor(login.error);
    sessionModel_.onReloginFailure(nowMs, reason);
    setSession(sessionModel_.state());
    return false;
  }

  const BambuCloudUserIdResult user = cloud_->fetchUserId(login.accessToken, config.region);
  if (!user.ok()) {
    const BambuReloginFailure reason = reloginFailureFor(user.error);
    sessionModel_.onReloginFailure(nowMs, reason);
    setSession(sessionModel_.state());
    return false;
  }

  BambuConfig updated = config;
  updated.accessToken = login.accessToken;
  updated.cloudUserId = user.userId;
  if (!persistConfig(updated)) {
    sessionModel_.onReloginFailure(nowMs, BambuReloginFailure::SERVICE_ERROR);
    setSession(sessionModel_.state());
    return false;
  }

  sessionModel_.onReloginSuccess();
  setSession(BambuSessionState::MQTT_CONNECTING);
  mqttAttempted_ = false;
  return true;
}

void BambuMqttService::disconnectMqtt() {
  if (mqtt_) {
    if (mqtt_->connected()) mqtt_->disconnect();
    delete mqtt_;
    mqtt_ = nullptr;
  }
  if (tls_) {
    tls_->stop();
    delete tls_;
    tls_ = nullptr;
  }
  setConnectivity(false);
}

void BambuMqttService::setConnectivity(bool connected) {
  if (!mutex_) return;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    state_.connected = connected;
    status_.mqttConnected = connected;
    xSemaphoreGive(mutex_);
  }
}

void BambuMqttService::setSession(BambuSessionState session, int mqttRc) {
  if (!mutex_) return;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    status_.session = session;
    if (mqttRc != -1) status_.lastMqttRc = mqttRc;
    status_.reloginFailureCount = sessionModel_.reloginFailureCount();
    status_.configured = config_.enabled && !config_.email.empty();
    status_.passwordSet = !config_.password.empty();
    status_.tokenSet = !config_.accessToken.empty();
    xSemaphoreGive(mutex_);
  }
}

BambuConfig BambuMqttService::configCopy() const {
  BambuConfig copy;
  if (!mutex_) return copy;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    copy = config_;
    xSemaphoreGive(mutex_);
  }
  return copy;
}

bool BambuMqttService::persistConfig(const BambuConfig& config) {
  if (!store_ || !store_->save(config)) return false;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    config_ = config;
    if (externalConfig_) *externalConfig_ = config;
    status_.configured = config_.enabled && !config_.email.empty();
    status_.passwordSet = !config_.password.empty();
    status_.tokenSet = !config_.accessToken.empty();
    xSemaphoreGive(mutex_);
  } else {
    return false;
  }
  return true;
}
