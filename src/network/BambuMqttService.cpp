#include "BambuMqttService.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#include <cstdio>
#include <new>
#include <string>
#include <string_view>

#include "BambuCloudProtocol.h"
#include "NetworkArbiter.h"
#include "build_config.h"

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

namespace {
constexpr uint16_t BAMBU_MQTT_PORT = 8883U;
constexpr uint16_t BAMBU_MQTT_KEEPALIVE_SEC = 30U;
constexpr uint32_t BAMBU_CLOUD_RECONNECT_BASE_MS = 30000U;
constexpr uint32_t BAMBU_CLOUD_RECONNECT_PHASE2_MS = 60000U;
constexpr uint32_t BAMBU_CLOUD_RECONNECT_PHASE3_MS = 120000U;
constexpr uint16_t BAMBU_BACKOFF_PHASE1_FAILS = 5U;
constexpr uint16_t BAMBU_BACKOFF_PHASE3_FAILS = 15U;
constexpr uint32_t BAMBU_PUSHALL_INITIAL_DELAY_MS = 2000U;

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

bool elapsed(uint32_t nowMs, uint32_t sinceMs, uint32_t intervalMs) {
  return static_cast<uint32_t>(nowMs - sinceMs) >= intervalMs;
}

bool configuredForMqtt(const BambuConfig& config) {
  return config.enabled && !config.accessToken.empty() && !config.cloudUserId.empty() &&
         activeBambuPrinter(config) != nullptr;
}
}  // namespace

BambuMqttService* BambuMqttService::activeInstance_ = nullptr;

bool BambuMqttService::begin(const BambuConfig& config, BambuConfigStore& store) {
  if (mutex_ || activeInstance_) return false;
  mutex_ = xSemaphoreCreateMutex();
  if (!mutex_) return false;

  store_ = &store;
  config_ = config;
  status_.configured = configuredForMqtt(config_);
  status_.tokenSet = !config_.accessToken.empty();
  status_.session = config_.enabled ? BambuSessionState::MQTT_CONNECTING
                                    : BambuSessionState::INTEGRATION_DISABLED;
  activeInstance_ = this;
  return true;
}

void BambuMqttService::process(uint32_t nowMs) {
  uint32_t revision = 0U;
  const BambuConfig config = configCopy(&revision);

  if (revision != observedExternalConfigRevision_) {
    observedExternalConfigRevision_ = revision;
    disconnectMqtt();
    mqttAttempted_ = false;
    tokenRejected_ = false;
    consecutiveFails_ = 0U;
    lastMqttAttemptMs_ = 0U;
    connectTimeMs_ = 0U;
  }

  if (!config.enabled) {
    if (mqtt_ || tls_) disconnectMqtt();
    setSession(BambuSessionState::INTEGRATION_DISABLED);
    return;
  }

  if (!configuredForMqtt(config)) {
    if (mqtt_ || tls_) disconnectMqtt();
    setSession(config.accessToken.empty() ? BambuSessionState::TOKEN_INVALID
                                          : BambuSessionState::UNCONFIGURED);
    return;
  }

  if (tokenRejected_) {
    if (mqtt_ || tls_) disconnectMqtt();
    setSession(BambuSessionState::TOKEN_INVALID);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (mqtt_ || tls_) disconnectMqtt();
    setSession(BambuSessionState::NETWORK_ERROR);
    return;
  }

  if (!mqtt_ || !mqtt_->connected()) {
    setConnectivity(false);
    if (!mqttAttempted_ || elapsed(nowMs, lastMqttAttemptMs_, reconnectIntervalMs())) {
      connectMqtt(nowMs);
    }
    return;
  }

  if (!mqtt_->loop()) {
    const int rc = mqtt_->state();
    if (consecutiveFails_ < UINT16_MAX) ++consecutiveFails_;
    setConnectivity(false);
    setSession(BambuSessionState::NETWORK_ERROR, rc);
    Serial.printf("[bambu] mqtt_loop_lost rc=%d wifi=%d rssi=%d heap=%u fails=%u\n",
                  rc, static_cast<int>(WiFi.status()), WiFi.RSSI(),
                  static_cast<unsigned>(esp_get_free_heap_size()),
                  static_cast<unsigned>(consecutiveFails_));
    disconnectMqtt();
    return;
  }

  if (initialPushallPending_ && connectTimeMs_ != 0U &&
      elapsed(nowMs, connectTimeMs_, BAMBU_PUSHALL_INITIAL_DELAY_MS)) {
    publishInitialPushall();
  }
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

BambuConfig BambuMqttService::configSnapshot() const { return configCopy(); }

void BambuMqttService::applyConfigLocked(const BambuConfig& config) {
  config_ = config;
  ++externalConfigRevision_;
  state_ = BambuState{};
  status_.mqttConnected = false;
  status_.lastMessageMs = 0U;
  status_.lastMqttRc = -1;
  status_.configured = configuredForMqtt(config_);
  status_.tokenSet = !config_.accessToken.empty();
  status_.session = config_.enabled ? BambuSessionState::MQTT_CONNECTING
                                    : BambuSessionState::INTEGRATION_DISABLED;
}

bool BambuMqttService::replaceConfig(const BambuConfig& config) {
  if (!store_ || !mutex_ || !validateBambuConfig(config).ok()) return false;
  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) return false;
  const bool saved = store_->save(config);
  if (saved) applyConfigLocked(config);
  xSemaphoreGive(mutex_);
  return saved;
}

bool BambuMqttService::cycleActivePrinter(int direction) {
  if (!store_ || !mutex_ || direction == 0) return false;
  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) return false;
  BambuConfig next = config_;
  if (!selectRelativeBambuPrinter(next, direction)) {
    xSemaphoreGive(mutex_);
    return false;
  }
  const bool saved = store_->save(next);
  if (saved) applyConfigLocked(next);
  xSemaphoreGive(mutex_);
  return saved;
}

void BambuMqttService::mqttCallbackThunk(char* topic, uint8_t* payload, unsigned int length) {
  esp_task_wdt_reset();
  if (activeInstance_) activeInstance_->handleMessage(topic, payload, length);
}

void BambuMqttService::handleMessage(const char* topic, const uint8_t* payload,
                                     unsigned int length) {
  const BambuConfig config = configCopy();
  const BambuPrinterConfig* active = activeBambuPrinter(config);
  if (!active) return;

  const std::string expected = bambuReportTopic(active->serial);
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

uint32_t BambuMqttService::reconnectIntervalMs() const {
  if (consecutiveFails_ >= BAMBU_BACKOFF_PHASE3_FAILS) {
    return BAMBU_CLOUD_RECONNECT_PHASE3_MS;
  }
  if (consecutiveFails_ >= BAMBU_BACKOFF_PHASE1_FAILS) {
    return BAMBU_CLOUD_RECONNECT_PHASE2_MS;
  }
  return BAMBU_CLOUD_RECONNECT_BASE_MS;
}

bool BambuMqttService::connectMqtt(uint32_t nowMs) {
  lastMqttAttemptMs_ = nowMs;
  mqttAttempted_ = true;

  const BambuConfig config = configCopy();
  const BambuPrinterConfig* active = activeBambuPrinter(config);
  if (!active || config.cloudUserId.empty() || config.accessToken.empty()) {
    setSession(BambuSessionState::UNCONFIGURED);
    return false;
  }

  // Adapted from Keralots/BambuHelper (MIT): Cloud reconnects always rebuild
  // both client objects so no stale TLS/socket/session state survives.
  disconnectMqtt();
  setSession(BambuSessionState::MQTT_CONNECTING);

  esp_task_wdt_reset();
  NetworkRequestGuard guard(sharedNetworkArbiter());
  if (!guard.locked()) {
    if (consecutiveFails_ < UINT16_MAX) ++consecutiveFails_;
    setSession(BambuSessionState::NETWORK_ERROR, -2);
    return false;
  }
  esp_task_wdt_reset();

  const char* broker = bambuBrokerForRegion(config.region);
  Serial.printf("[bambu] mqtt_connect broker=%s wifi=%d rssi=%d heap=%u fails=%u\n",
                broker, static_cast<int>(WiFi.status()), WiFi.RSSI(),
                static_cast<unsigned>(esp_get_free_heap_size()),
                static_cast<unsigned>(consecutiveFails_));

  tls_ = new (std::nothrow) WiFiClientSecure();
  if (!tls_) {
    if (consecutiveFails_ < UINT16_MAX) ++consecutiveFails_;
    setSession(BambuSessionState::BUFFER_ERROR, -2);
    disconnectMqtt();
    return false;
  }
  tls_->setCACertBundle(rootca_crt_bundle_start);
  tls_->setTimeout(15);

  mqtt_ = new (std::nothrow) PubSubClient(*tls_);
  if (!mqtt_) {
    if (consecutiveFails_ < UINT16_MAX) ++consecutiveFails_;
    setSession(BambuSessionState::BUFFER_ERROR, -2);
    disconnectMqtt();
    return false;
  }
  mqtt_->setServer(broker, BAMBU_MQTT_PORT);
  mqtt_->setCallback(mqttCallbackThunk);
  mqtt_->setKeepAlive(BAMBU_MQTT_KEEPALIVE_SEC);
  if (!mqtt_->setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)) {
    if (consecutiveFails_ < UINT16_MAX) ++consecutiveFails_;
    setSession(BambuSessionState::BUFFER_ERROR, -2);
    disconnectMqtt();
    return false;
  }

  char clientId[32];
  std::snprintf(clientId, sizeof(clientId), "bblp_%08x%04x",
                static_cast<unsigned>(esp_random()),
                static_cast<unsigned>(esp_random() & 0xFFFFU));

  const uint32_t startedMs = millis();
  esp_task_wdt_reset();
  const bool connected =
      mqtt_->connect(clientId, config.cloudUserId.c_str(), config.accessToken.c_str());
  const uint32_t connectElapsedMs = static_cast<uint32_t>(millis() - startedMs);

  if (!connected) {
    const int rc = mqtt_->state();
    if (rc == 4 || rc == 5) {
      tokenRejected_ = true;
    } else if (consecutiveFails_ < UINT16_MAX) {
      ++consecutiveFails_;
    }
    Serial.printf(
        "[bambu] mqtt_connect_fail rc=%d elapsed_ms=%lu wifi=%d rssi=%d heap=%u fails=%u retry_ms=%lu\n",
        rc, static_cast<unsigned long>(connectElapsedMs), static_cast<int>(WiFi.status()),
        WiFi.RSSI(), static_cast<unsigned>(esp_get_free_heap_size()),
        static_cast<unsigned>(consecutiveFails_),
        static_cast<unsigned long>(reconnectIntervalMs()));
    setSession((rc == 4 || rc == 5) ? BambuSessionState::TOKEN_INVALID
                                    : BambuSessionState::NETWORK_ERROR,
               rc);
    disconnectMqtt();
    return false;
  }

  const std::string reportTopic = bambuReportTopic(active->serial);
  if (reportTopic.empty() || !mqtt_->subscribe(reportTopic.c_str())) {
    const int rc = mqtt_->state();
    if (consecutiveFails_ < UINT16_MAX) ++consecutiveFails_;
    Serial.printf("[bambu] mqtt_subscribe_fail rc=%d wifi=%d rssi=%d heap=%u fails=%u\n",
                  rc, static_cast<int>(WiFi.status()), WiFi.RSSI(),
                  static_cast<unsigned>(esp_get_free_heap_size()),
                  static_cast<unsigned>(consecutiveFails_));
    setSession(BambuSessionState::NETWORK_ERROR, rc);
    disconnectMqtt();
    return false;
  }

  consecutiveFails_ = 0U;
  tokenRejected_ = false;
  connectTimeMs_ = millis();
  initialPushallPending_ = true;
  setConnectivity(true);
  setSession(BambuSessionState::ONLINE, 0);
  Serial.printf("[bambu] mqtt_connect_ok elapsed_ms=%lu rssi=%d heap=%u\n",
                static_cast<unsigned long>(connectElapsedMs), WiFi.RSSI(),
                static_cast<unsigned>(esp_get_free_heap_size()));
  return true;
}

bool BambuMqttService::publishInitialPushall() {
  if (!mqtt_ || !mqtt_->connected()) return false;

  const BambuConfig config = configCopy();
  const BambuPrinterConfig* active = activeBambuPrinter(config);
  if (!active) return false;

  const std::string requestTopic = "device/" + active->serial + "/request";
  const uint32_t sequence = pushallSequence_++;
  char request[144];
  std::snprintf(request, sizeof(request),
                "{\"pushing\":{\"sequence_id\":\"%lu\",\"command\":\"pushall\",\"version\":1,\"push_target\":1}}",
                static_cast<unsigned long>(sequence));

  esp_task_wdt_reset();
  const bool published = mqtt_->publish(requestTopic.c_str(), request);
  if (published) {
    initialPushallPending_ = false;
    Serial.printf("[bambu] mqtt_pushall_initial seq=%lu delay_ms=%lu\n",
                  static_cast<unsigned long>(sequence),
                  static_cast<unsigned long>(millis() - connectTimeMs_));
  } else {
    Serial.printf("[bambu] mqtt_pushall_initial_fail rc=%d\n", mqtt_->state());
  }
  return published;
}

void BambuMqttService::disconnectMqtt() {
  initialPushallPending_ = false;
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
    status_.configured = configuredForMqtt(config_);
    status_.tokenSet = !config_.accessToken.empty();
    xSemaphoreGive(mutex_);
  }
}

BambuConfig BambuMqttService::configCopy(uint32_t* externalRevision) const {
  BambuConfig copy;
  if (!mutex_) return copy;
  if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
    copy = config_;
    if (externalRevision) *externalRevision = externalConfigRevision_;
    xSemaphoreGive(mutex_);
  }
  return copy;
}
