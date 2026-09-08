#include "BambuMqttService.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

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
constexpr uint32_t BAMBU_MQTT_RECONNECT_MS = 30000U;
constexpr uint32_t BAMBU_TASK_SLEEP_MS = 50U;
constexpr uint32_t BAMBU_WIFI_WAIT_MS = 500U;
constexpr int32_t BAMBU_DIAG_PROBE_TIMEOUT_MS = 5000;

class NetworkRequestGuard {
 public:
  explicit NetworkRequestGuard(NetworkArbiter& arbiter)
      : arbiter_(arbiter), locked_(arbiter_.lock()) {}
  ~NetworkRequestGuard() { if (locked_) arbiter_.unlock(); }
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

void logHeapDiagnostic(const char* phase) {
  const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t internalLargest =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const size_t dmaFree = heap_caps_get_free_size(MALLOC_CAP_DMA);
  Serial.printf(
      "[bambu] mqtt_diag_heap phase=%s internal_free=%u internal_largest=%u dma_free=%u heap_free=%u\n",
      phase, static_cast<unsigned>(internalFree), static_cast<unsigned>(internalLargest),
      static_cast<unsigned>(dmaFree), static_cast<unsigned>(esp_get_free_heap_size()));
}

void runLayeredConnectionProbe(const char* broker) {
  logHeapDiagnostic("before_probe");

  IPAddress resolvedIp;
  const uint32_t dnsStartedMs = millis();
  const int dnsOk = WiFi.hostByName(broker, resolvedIp);
  const uint32_t dnsElapsedMs = static_cast<uint32_t>(millis() - dnsStartedMs);
  const String resolvedText = dnsOk == 1 ? resolvedIp.toString() : String("0.0.0.0");
  Serial.printf("[bambu] mqtt_diag_dns ok=%d ip=%s elapsed_ms=%lu\n",
                dnsOk == 1 ? 1 : 0, resolvedText.c_str(),
                static_cast<unsigned long>(dnsElapsedMs));

  if (dnsOk == 1) {
    WiFiClient tcpProbe;
    const uint32_t tcpStartedMs = millis();
    const int tcpOk = tcpProbe.connect(resolvedIp, BAMBU_MQTT_PORT, BAMBU_DIAG_PROBE_TIMEOUT_MS);
    const uint32_t tcpElapsedMs = static_cast<uint32_t>(millis() - tcpStartedMs);
    Serial.printf("[bambu] mqtt_diag_tcp ok=%d ip=%s port=%u elapsed_ms=%lu\n",
                  tcpOk == 1 ? 1 : 0, resolvedText.c_str(),
                  static_cast<unsigned>(BAMBU_MQTT_PORT),
                  static_cast<unsigned long>(tcpElapsedMs));
    tcpProbe.stop();
  } else {
    Serial.printf("[bambu] mqtt_diag_tcp ok=0 ip=unresolved port=%u elapsed_ms=0\n",
                  static_cast<unsigned>(BAMBU_MQTT_PORT));
  }

  WiFiClientSecure tlsProbe;
  tlsProbe.setCACertBundle(rootca_crt_bundle_start);
  tlsProbe.setHandshakeTimeout(BuildConfig::HTTP_TLS_HANDSHAKE_TIMEOUT_SEC);
  tlsProbe.setTimeout(5);
  const uint32_t tlsStartedMs = millis();
  const int tlsOk = tlsProbe.connect(broker, BAMBU_MQTT_PORT, BAMBU_DIAG_PROBE_TIMEOUT_MS);
  const uint32_t tlsElapsedMs = static_cast<uint32_t>(millis() - tlsStartedMs);
  char tlsErrorText[96] = {};
  const int tlsError = tlsOk == 1 ? 0 : tlsProbe.lastError(tlsErrorText, sizeof(tlsErrorText));
  Serial.printf("[bambu] mqtt_diag_tls ok=%d tls=%d elapsed_ms=%lu\n",
                tlsOk == 1 ? 1 : 0, tlsError, static_cast<unsigned long>(tlsElapsedMs));
  tlsProbe.stop();

  logHeapDiagnostic("after_probe");
}
}  // namespace

BambuMqttService* BambuMqttService::activeInstance_ = nullptr;

bool BambuMqttService::begin(const BambuConfig& config, BambuConfigStore& store) {
  if (task_ || activeInstance_) return false;
  mutex_ = xSemaphoreCreateMutex();
  if (!mutex_) return false;
  store_ = &store;
  config_ = config;
  status_.configured = configuredForMqtt(config_);
  status_.tokenSet = !config_.accessToken.empty();
  status_.session = config_.enabled ? BambuSessionState::MQTT_CONNECTING
                                    : BambuSessionState::INTEGRATION_DISABLED;
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

void BambuMqttService::taskThunk(void* arg) { static_cast<BambuMqttService*>(arg)->taskLoop(); }
void BambuMqttService::mqttCallbackThunk(char* topic, uint8_t* payload, unsigned int length) {
  if (activeInstance_) activeInstance_->handleMessage(topic, payload, length);
}

void BambuMqttService::taskLoop() {
  for (;;) {
    const uint32_t nowMs = millis();
    uint32_t revision = 0U;
    const BambuConfig config = configCopy(&revision);
    if (revision != observedExternalConfigRevision_) {
      observedExternalConfigRevision_ = revision;
      disconnectMqtt();
      mqttAttempted_ = false;
      tokenRejected_ = false;
    }
    if (!config.enabled) {
      disconnectMqtt();
      setSession(BambuSessionState::INTEGRATION_DISABLED);
      vTaskDelay(pdMS_TO_TICKS(BAMBU_WIFI_WAIT_MS));
      continue;
    }
    if (!configuredForMqtt(config)) {
      disconnectMqtt();
      setSession(config.accessToken.empty() ? BambuSessionState::TOKEN_INVALID
                                            : BambuSessionState::UNCONFIGURED);
      vTaskDelay(pdMS_TO_TICKS(BAMBU_WIFI_WAIT_MS));
      continue;
    }
    if (tokenRejected_) {
      disconnectMqtt();
      setSession(BambuSessionState::TOKEN_INVALID);
      vTaskDelay(pdMS_TO_TICKS(BAMBU_WIFI_WAIT_MS));
      continue;
    }
    if (WiFi.status() != WL_CONNECTED) {
      disconnectMqtt();
      setSession(BambuSessionState::NETWORK_ERROR);
      vTaskDelay(pdMS_TO_TICKS(BAMBU_WIFI_WAIT_MS));
      continue;
    }
    if (!mqtt_ || !mqtt_->connected()) {
      setConnectivity(false);
      if (!mqttAttempted_ || elapsed(nowMs, lastMqttAttemptMs_, BAMBU_MQTT_RECONNECT_MS)) connectMqtt(nowMs);
      vTaskDelay(pdMS_TO_TICKS(BAMBU_TASK_SLEEP_MS));
      continue;
    }
    if (!mqtt_->loop()) {
      const int rc = mqtt_->state();
      setConnectivity(false);
      setSession(BambuSessionState::NETWORK_ERROR, rc);
      Serial.printf("[bambu] mqtt_loop_lost rc=%d wifi=%d rssi=%d heap=%u\n",
                    rc, static_cast<int>(WiFi.status()), WiFi.RSSI(),
                    static_cast<unsigned>(esp_get_free_heap_size()));
    }
    vTaskDelay(pdMS_TO_TICKS(BAMBU_TASK_SLEEP_MS));
  }
}

void BambuMqttService::handleMessage(const char* topic, const uint8_t* payload, unsigned int length) {
  const BambuConfig config = configCopy();
  const BambuPrinterConfig* active = activeBambuPrinter(config);
  if (!active) return;
  const std::string expected = bambuReportTopic(active->serial);
  if (!topic || expected.empty() || expected != topic || !payload || length == 0U || length > BambuStateLimits::REPORT_JSON) return;
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

bool BambuMqttService::connectMqtt(uint32_t nowMs) {
  lastMqttAttemptMs_ = nowMs;
  mqttAttempted_ = true;
  const BambuConfig config = configCopy();
  const BambuPrinterConfig* active = activeBambuPrinter(config);
  if (!active || config.cloudUserId.empty() || config.accessToken.empty()) {
    setSession(BambuSessionState::UNCONFIGURED);
    return false;
  }

  disconnectMqtt();
  setSession(BambuSessionState::MQTT_CONNECTING);
  NetworkRequestGuard guard(sharedNetworkArbiter());
  if (!guard.locked()) { setSession(BambuSessionState::NETWORK_ERROR); return false; }

  const char* broker = bambuBrokerForRegion(config.region);
  Serial.printf("[bambu] mqtt_connect broker=%s wifi=%d rssi=%d heap=%u\n",
                broker, static_cast<int>(WiFi.status()), WiFi.RSSI(),
                static_cast<unsigned>(esp_get_free_heap_size()));

  runLayeredConnectionProbe(broker);

  tls_ = new (std::nothrow) WiFiClientSecure();
  if (!tls_) { setSession(BambuSessionState::BUFFER_ERROR); return false; }
  tls_->setCACertBundle(rootca_crt_bundle_start);
  tls_->setHandshakeTimeout(BuildConfig::HTTP_TLS_HANDSHAKE_TIMEOUT_SEC);
  tls_->setTimeout(15);
  mqtt_ = new (std::nothrow) PubSubClient(*tls_);
  if (!mqtt_) { disconnectMqtt(); setSession(BambuSessionState::BUFFER_ERROR); return false; }
  mqtt_->setServer(broker, BAMBU_MQTT_PORT);
  mqtt_->setCallback(mqttCallbackThunk);
  mqtt_->setKeepAlive(BAMBU_MQTT_KEEPALIVE_SEC);
  if (!mqtt_->setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)) {
    disconnectMqtt();
    setSession(BambuSessionState::BUFFER_ERROR);
    return false;
  }
  logHeapDiagnostic("after_mqtt_buffer");

  char clientId[32];
  std::snprintf(clientId, sizeof(clientId), "tdgp_%08lx%04x",
                static_cast<unsigned long>(esp_random()), static_cast<unsigned>(esp_random() & 0xFFFFU));
  const uint32_t mqttStartedMs = millis();
  if (!mqtt_->connect(clientId, config.cloudUserId.c_str(), config.accessToken.c_str())) {
    const uint32_t mqttElapsedMs = static_cast<uint32_t>(millis() - mqttStartedMs);
    const int rc = mqtt_->state();
    char tlsErrorText[96] = {};
    const int tlsError = tls_->lastError(tlsErrorText, sizeof(tlsErrorText));
    const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t internalLargest =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t dmaFree = heap_caps_get_free_size(MALLOC_CAP_DMA);
    Serial.printf(
        "[bambu] mqtt_connect_fail rc=%d tls=%d elapsed_ms=%lu wifi=%d rssi=%d heap=%u internal_free=%u internal_largest=%u dma_free=%u\n",
        rc, tlsError, static_cast<unsigned long>(mqttElapsedMs), static_cast<int>(WiFi.status()),
        WiFi.RSSI(), static_cast<unsigned>(esp_get_free_heap_size()),
        static_cast<unsigned>(internalFree), static_cast<unsigned>(internalLargest),
        static_cast<unsigned>(dmaFree));
    if (rc == 4 || rc == 5) tokenRejected_ = true;
    setSession((rc == 4 || rc == 5) ? BambuSessionState::TOKEN_INVALID : BambuSessionState::NETWORK_ERROR, rc);
    disconnectMqtt();
    return false;
  }
  const uint32_t mqttElapsedMs = static_cast<uint32_t>(millis() - mqttStartedMs);

  const std::string reportTopic = bambuReportTopic(active->serial);
  if (reportTopic.empty() || !mqtt_->subscribe(reportTopic.c_str())) {
    const int rc = mqtt_->state();
    Serial.printf("[bambu] mqtt_subscribe_fail rc=%d wifi=%d rssi=%d heap=%u\n",
                  rc, static_cast<int>(WiFi.status()), WiFi.RSSI(),
                  static_cast<unsigned>(esp_get_free_heap_size()));
    setSession(BambuSessionState::NETWORK_ERROR, rc);
    disconnectMqtt();
    return false;
  }
  const std::string requestTopic = "device/" + active->serial + "/request";
  char request[144];
  std::snprintf(request, sizeof(request),
                "{\"pushing\":{\"sequence_id\":\"%lu\",\"command\":\"pushall\",\"version\":1,\"push_target\":1}}",
                static_cast<unsigned long>(pushallSequence_++));
  mqtt_->publish(requestTopic.c_str(), request);
  tokenRejected_ = false;
  setConnectivity(true);
  setSession(BambuSessionState::ONLINE, 0);
  Serial.printf("[bambu] mqtt_connect_ok elapsed_ms=%lu rssi=%d heap=%u\n",
                static_cast<unsigned long>(mqttElapsedMs), WiFi.RSSI(),
                static_cast<unsigned>(esp_get_free_heap_size()));
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
