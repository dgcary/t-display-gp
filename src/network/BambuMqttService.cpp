#include "BambuMqttService.h"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>

#include <cstdio>
#include <new>
#include <string>

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
constexpr uint32_t BAMBU_TASK_SLEEP_MS = 50U;

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
         config.printerCount > 0U;
}

bool sameConnectionSet(const BambuConfig& lhs, const BambuConfig& rhs) {
  if (lhs.enabled != rhs.enabled || lhs.region != rhs.region ||
      lhs.accessToken != rhs.accessToken || lhs.cloudUserId != rhs.cloudUserId ||
      lhs.printerCount != rhs.printerCount) {
    return false;
  }
  for (size_t slot = 0; slot < lhs.printerCount; ++slot) {
    if (lhs.printers[slot].serial != rhs.printers[slot].serial) return false;
  }
  return true;
}
}  // namespace

BambuMqttService* BambuMqttService::activeInstance_ = nullptr;

bool BambuMqttService::begin(const BambuConfig& config, BambuConfigStore& store) {
  if (task_ || mutex_ || activeInstance_) return false;
  mutex_ = xSemaphoreCreateMutex();
  if (!mutex_) return false;

  store_ = &store;
  config_ = config;
  for (size_t slot = 0; slot < BambuConfigLimits::PRINTER_COUNT; ++slot) {
    resetSlotRuntime(slot, config_);
  }

  activeInstance_ = this;
  const BaseType_t created = xTaskCreatePinnedToCore(
      taskThunk, "bambu-mqtt", 8192, this, 1, &task_, 0);
  if (created != pdPASS) {
    activeInstance_ = nullptr;
    task_ = nullptr;
    store_ = nullptr;
    vSemaphoreDelete(mutex_);
    mutex_ = nullptr;
    return false;
  }
  return true;
}

void BambuMqttService::taskThunk(void* arg) {
  static_cast<BambuMqttService*>(arg)->taskLoop();
}

void BambuMqttService::taskLoop() {
  for (;;) {
    process(millis());
    vTaskDelay(pdMS_TO_TICKS(BAMBU_TASK_SLEEP_MS));
  }
}

void BambuMqttService::process(uint32_t nowMs) {
  uint32_t revision = 0U;
  const BambuConfig config = configCopy(&revision);

  if (revision != observedExternalConfigRevision_) {
    observedExternalConfigRevision_ = revision;
    disconnectAll();
    for (size_t slot = 0; slot < BambuConfigLimits::PRINTER_COUNT; ++slot) {
      resetSlotRuntime(slot, config);
    }
  }

  if (!config.enabled) {
    disconnectAll();
    for (size_t slot = 0; slot < BambuConfigLimits::PRINTER_COUNT; ++slot) {
      setSlotSession(slot, BambuSessionState::INTEGRATION_DISABLED);
    }
    return;
  }

  if (!configuredForMqtt(config)) {
    disconnectAll();
    const BambuSessionState session = config.accessToken.empty()
                                          ? BambuSessionState::TOKEN_INVALID
                                          : BambuSessionState::UNCONFIGURED;
    for (size_t slot = 0; slot < BambuConfigLimits::PRINTER_COUNT; ++slot) {
      setSlotSession(slot, session);
    }
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    for (size_t slot = 0; slot < config.printerCount; ++slot) {
      disconnectSlot(slot);
      setSlotSession(slot, BambuSessionState::NETWORK_ERROR);
    }
    return;
  }

  // Service all already-established sockets before starting any potentially
  // blocking new Cloud connection. The blocking connect itself runs only on
  // the dedicated Bambu worker, never on the Arduino UI loop.
  for (size_t slot = 0; slot < config.printerCount; ++slot) {
    MqttConn& conn = conns_[slot];
    if (!conn.mqtt || !conn.mqtt->connected()) continue;

    if (!conn.mqtt->loop()) {
      const int rc = conn.mqtt->state();
      if (conn.consecutiveFails < UINT16_MAX) ++conn.consecutiveFails;
      conn.lastMqttAttemptMs = millis();
      conn.mqttAttempted = true;
      setSlotConnectivity(slot, false);
      setSlotSession(slot, BambuSessionState::NETWORK_ERROR, rc);
      Serial.printf("[bambu] mqtt_loop_lost slot=%u rc=%d wifi=%d rssi=%d heap=%u fails=%u retry_ms=%lu\n",
                    static_cast<unsigned>(slot), rc, static_cast<int>(WiFi.status()),
                    WiFi.RSSI(), static_cast<unsigned>(esp_get_free_heap_size()),
                    static_cast<unsigned>(conn.consecutiveFails),
                    static_cast<unsigned long>(reconnectIntervalMs(conn)));
      disconnectSlot(slot);
      continue;
    }

    if (conn.initialPushallPending && conn.connectTimeMs != 0U &&
        elapsed(nowMs, conn.connectTimeMs, BAMBU_PUSHALL_INITIAL_DELAY_MS)) {
      publishInitialPushall(slot);
    }
  }

  // At most one new/reconnect attempt per worker pass. Prefer the currently
  // selected printer, then walk the remaining configured slots.
  for (size_t offset = 0; offset < config.printerCount; ++offset) {
    const size_t slot = (config.activePrinterIndex + offset) % config.printerCount;
    MqttConn& conn = conns_[slot];
    if (conn.mqtt && conn.mqtt->connected()) continue;
    if (conn.tokenRejected) {
      setSlotSession(slot, BambuSessionState::TOKEN_INVALID, conn.status.lastMqttRc);
      continue;
    }

    setSlotConnectivity(slot, false);
    if (!conn.mqttAttempted ||
        elapsed(nowMs, conn.lastMqttAttemptMs, reconnectIntervalMs(conn))) {
      connectSlot(slot, nowMs);
      return;
    }
  }
}

BambuState BambuMqttService::snapshot() const {
  BambuState copy;
  if (!mutex_) return copy;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    const size_t slot = config_.activePrinterIndex < config_.printerCount
                            ? config_.activePrinterIndex
                            : 0U;
    copy = states_[slot];
    xSemaphoreGive(mutex_);
  }
  return copy;
}

BambuMqttStatus BambuMqttService::status() const {
  BambuMqttStatus copy;
  if (!mutex_) return copy;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    const size_t slot = config_.activePrinterIndex < config_.printerCount
                            ? config_.activePrinterIndex
                            : 0U;
    copy = conns_[slot].status;
    copy.configured = configuredForMqtt(config_);
    copy.tokenSet = !config_.accessToken.empty();
    xSemaphoreGive(mutex_);
  }
  return copy;
}

BambuConfig BambuMqttService::configSnapshot() const { return configCopy(); }

void BambuMqttService::applyConfigLocked(const BambuConfig& config) {
  config_ = config;
  ++externalConfigRevision_;
  for (size_t slot = 0; slot < BambuConfigLimits::PRINTER_COUNT; ++slot) {
    states_[slot] = BambuState{};
    conns_[slot].status.mqttConnected = false;
    conns_[slot].status.lastMessageMs = 0U;
    conns_[slot].status.lastMqttRc = -1;
    conns_[slot].status.configured = configuredForMqtt(config_) && slot < config_.printerCount;
    conns_[slot].status.tokenSet = !config_.accessToken.empty();
    conns_[slot].status.session = config_.enabled
                                      ? BambuSessionState::MQTT_CONNECTING
                                      : BambuSessionState::INTEGRATION_DISABLED;
  }
}

bool BambuMqttService::replaceConfig(const BambuConfig& config) {
  if (!store_ || !mutex_ || !validateBambuConfig(config).ok()) return false;
  if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) return false;

  const bool connectionSetUnchanged = sameConnectionSet(config_, config);
  const bool saved = store_->save(config);
  if (saved) {
    if (connectionSetUnchanged) {
      // Name/active-slot changes are presentation/config changes only. Keep all
      // persistent MQTT sockets and their per-printer cached state alive.
      config_ = config;
      for (size_t slot = 0; slot < BambuConfigLimits::PRINTER_COUNT; ++slot) {
        conns_[slot].status.configured = configuredForMqtt(config_) && slot < config_.printerCount;
        conns_[slot].status.tokenSet = !config_.accessToken.empty();
      }
    } else {
      applyConfigLocked(config);
    }
  }

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
  if (saved) {
    // Switching printers is intentionally a local active-slot selection only.
    // Each configured printer keeps its own TLS/MQTT connection and cached
    // BambuState, matching upstream BambuHelper's simultaneous-connection model.
    config_ = next;
  }

  xSemaphoreGive(mutex_);
  return saved;
}

void BambuMqttService::mqttCallbackThunk(char* topic, uint8_t* payload, unsigned int length) {
  if (activeInstance_) activeInstance_->handleMessage(topic, payload, length);
}

size_t BambuMqttService::findSlotForTopic(const char* topic) const {
  if (!topic) return BambuConfigLimits::PRINTER_COUNT;
  const BambuConfig config = configCopy();
  for (size_t slot = 0; slot < config.printerCount; ++slot) {
    const std::string expected = bambuReportTopic(config.printers[slot].serial);
    if (!expected.empty() && expected == topic) return slot;
  }
  return BambuConfigLimits::PRINTER_COUNT;
}

void BambuMqttService::handleMessage(const char* topic, const uint8_t* payload,
                                     unsigned int length) {
  const size_t slot = findSlotForTopic(topic);
  if (slot >= BambuConfigLimits::PRINTER_COUNT || !payload || length == 0U ||
      length > BambuStateLimits::REPORT_JSON) {
    return;
  }

  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return;
  const std::string_view json(reinterpret_cast<const char*>(payload), length);
  if (applyBambuReport(json, millis(), states_[slot])) {
    states_[slot].connected = true;
    conns_[slot].status.mqttConnected = true;
    conns_[slot].status.session = BambuSessionState::ONLINE;
    conns_[slot].status.lastMessageMs = states_[slot].lastUpdateMs;
    conns_[slot].status.lastMqttRc = 0;
  }
  xSemaphoreGive(mutex_);
}

uint32_t BambuMqttService::reconnectIntervalMs(const MqttConn& conn) const {
  if (conn.consecutiveFails >= BAMBU_BACKOFF_PHASE3_FAILS) {
    return BAMBU_CLOUD_RECONNECT_PHASE3_MS;
  }
  if (conn.consecutiveFails >= BAMBU_BACKOFF_PHASE1_FAILS) {
    return BAMBU_CLOUD_RECONNECT_PHASE2_MS;
  }
  return BAMBU_CLOUD_RECONNECT_BASE_MS;
}

bool BambuMqttService::connectSlot(size_t slot, uint32_t nowMs) {
  if (slot >= BambuConfigLimits::PRINTER_COUNT) return false;
  static_cast<void>(nowMs);
  MqttConn& conn = conns_[slot];
  auto markAttemptComplete = [&conn]() {
    conn.lastMqttAttemptMs = millis();
    conn.mqttAttempted = true;
  };

  const BambuConfig config = configCopy();
  if (slot >= config.printerCount || config.cloudUserId.empty() || config.accessToken.empty()) {
    markAttemptComplete();
    setSlotSession(slot, BambuSessionState::UNCONFIGURED);
    return false;
  }

  disconnectSlot(slot);
  setSlotSession(slot, BambuSessionState::MQTT_CONNECTING);

  NetworkRequestGuard guard(sharedNetworkArbiter());
  if (!guard.locked()) {
    if (conn.consecutiveFails < UINT16_MAX) ++conn.consecutiveFails;
    markAttemptComplete();
    setSlotSession(slot, BambuSessionState::NETWORK_ERROR, -2);
    return false;
  }

  const char* broker = bambuBrokerForRegion(config.region);
  Serial.printf("[bambu] mqtt_connect slot=%u broker=%s wifi=%d rssi=%d heap=%u fails=%u\n",
                static_cast<unsigned>(slot), broker, static_cast<int>(WiFi.status()),
                WiFi.RSSI(), static_cast<unsigned>(esp_get_free_heap_size()),
                static_cast<unsigned>(conn.consecutiveFails));

  conn.tls = new (std::nothrow) WiFiClientSecure();
  if (!conn.tls) {
    if (conn.consecutiveFails < UINT16_MAX) ++conn.consecutiveFails;
    markAttemptComplete();
    setSlotSession(slot, BambuSessionState::BUFFER_ERROR, -2);
    disconnectSlot(slot);
    return false;
  }
  conn.tls->setCACertBundle(rootca_crt_bundle_start);
  conn.tls->setTimeout(BuildConfig::BAMBU_MQTT_CONNECT_TIMEOUT_SEC);
  conn.tls->setHandshakeTimeout(BuildConfig::BAMBU_MQTT_CONNECT_TIMEOUT_SEC);

  conn.mqtt = new (std::nothrow) PubSubClient(*conn.tls);
  if (!conn.mqtt) {
    if (conn.consecutiveFails < UINT16_MAX) ++conn.consecutiveFails;
    markAttemptComplete();
    setSlotSession(slot, BambuSessionState::BUFFER_ERROR, -2);
    disconnectSlot(slot);
    return false;
  }
  conn.mqtt->setServer(broker, BAMBU_MQTT_PORT);
  conn.mqtt->setCallback(mqttCallbackThunk);
  conn.mqtt->setKeepAlive(BAMBU_MQTT_KEEPALIVE_SEC);
  conn.mqtt->setSocketTimeout(BuildConfig::BAMBU_MQTT_CONNECT_TIMEOUT_SEC);
  if (!conn.mqtt->setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)) {
    if (conn.consecutiveFails < UINT16_MAX) ++conn.consecutiveFails;
    markAttemptComplete();
    setSlotSession(slot, BambuSessionState::BUFFER_ERROR, -2);
    disconnectSlot(slot);
    return false;
  }

  char clientId[32];
  std::snprintf(clientId, sizeof(clientId), "bblp_%08x%04x",
                static_cast<unsigned>(esp_random()),
                static_cast<unsigned>(esp_random() & 0xFFFFU));

  const uint32_t startedMs = millis();
  const bool connected = conn.mqtt->connect(
      clientId, config.cloudUserId.c_str(), config.accessToken.c_str());
  const uint32_t connectElapsedMs = static_cast<uint32_t>(millis() - startedMs);
  markAttemptComplete();

  if (!connected) {
    const int rc = conn.mqtt->state();
    if (rc == 4 || rc == 5) {
      conn.tokenRejected = true;
    } else if (conn.consecutiveFails < UINT16_MAX) {
      ++conn.consecutiveFails;
    }
    Serial.printf(
        "[bambu] mqtt_connect_fail slot=%u rc=%d elapsed_ms=%lu wifi=%d rssi=%d heap=%u fails=%u retry_ms=%lu\n",
        static_cast<unsigned>(slot), rc, static_cast<unsigned long>(connectElapsedMs),
        static_cast<int>(WiFi.status()), WiFi.RSSI(),
        static_cast<unsigned>(esp_get_free_heap_size()),
        static_cast<unsigned>(conn.consecutiveFails),
        static_cast<unsigned long>(reconnectIntervalMs(conn)));
    setSlotSession(slot, (rc == 4 || rc == 5) ? BambuSessionState::TOKEN_INVALID
                                              : BambuSessionState::NETWORK_ERROR,
                   rc);
    disconnectSlot(slot);
    return false;
  }

  const std::string reportTopic = bambuReportTopic(config.printers[slot].serial);
  if (reportTopic.empty() || !conn.mqtt->subscribe(reportTopic.c_str())) {
    const int rc = conn.mqtt->state();
    if (conn.consecutiveFails < UINT16_MAX) ++conn.consecutiveFails;
    markAttemptComplete();
    Serial.printf(
        "[bambu] mqtt_subscribe_fail slot=%u rc=%d wifi=%d rssi=%d heap=%u fails=%u retry_ms=%lu\n",
        static_cast<unsigned>(slot), rc, static_cast<int>(WiFi.status()), WiFi.RSSI(),
        static_cast<unsigned>(esp_get_free_heap_size()),
        static_cast<unsigned>(conn.consecutiveFails),
        static_cast<unsigned long>(reconnectIntervalMs(conn)));
    setSlotSession(slot, BambuSessionState::NETWORK_ERROR, rc);
    disconnectSlot(slot);
    return false;
  }

  conn.consecutiveFails = 0U;
  conn.tokenRejected = false;
  conn.connectTimeMs = millis();
  conn.initialPushallPending = true;
  setSlotConnectivity(slot, true);
  setSlotSession(slot, BambuSessionState::ONLINE, 0);
  Serial.printf("[bambu] mqtt_connect_ok slot=%u elapsed_ms=%lu rssi=%d heap=%u\n",
                static_cast<unsigned>(slot), static_cast<unsigned long>(connectElapsedMs),
                WiFi.RSSI(), static_cast<unsigned>(esp_get_free_heap_size()));
  return true;
}

bool BambuMqttService::publishInitialPushall(size_t slot) {
  if (slot >= BambuConfigLimits::PRINTER_COUNT) return false;
  MqttConn& conn = conns_[slot];
  if (!conn.mqtt || !conn.mqtt->connected()) return false;

  const BambuConfig config = configCopy();
  if (slot >= config.printerCount) return false;

  const std::string requestTopic = "device/" + config.printers[slot].serial + "/request";
  const uint32_t sequence = conn.pushallSequence++;
  char request[144];
  std::snprintf(request, sizeof(request),
                "{\"pushing\":{\"sequence_id\":\"%lu\",\"command\":\"pushall\",\"version\":1,\"push_target\":1}}",
                static_cast<unsigned long>(sequence));

  const bool published = conn.mqtt->publish(requestTopic.c_str(), request);
  if (published) {
    conn.initialPushallPending = false;
    Serial.printf("[bambu] mqtt_pushall_initial slot=%u seq=%lu delay_ms=%lu\n",
                  static_cast<unsigned>(slot), static_cast<unsigned long>(sequence),
                  static_cast<unsigned long>(millis() - conn.connectTimeMs));
  } else {
    Serial.printf("[bambu] mqtt_pushall_initial_fail slot=%u rc=%d\n",
                  static_cast<unsigned>(slot), conn.mqtt->state());
  }
  return published;
}

void BambuMqttService::disconnectSlot(size_t slot) {
  if (slot >= BambuConfigLimits::PRINTER_COUNT) return;
  MqttConn& conn = conns_[slot];
  conn.initialPushallPending = false;
  if (conn.mqtt) {
    if (conn.mqtt->connected()) conn.mqtt->disconnect();
    delete conn.mqtt;
    conn.mqtt = nullptr;
  }
  if (conn.tls) {
    conn.tls->stop();
    delete conn.tls;
    conn.tls = nullptr;
  }
  setSlotConnectivity(slot, false);
}

void BambuMqttService::disconnectAll() {
  for (size_t slot = 0; slot < BambuConfigLimits::PRINTER_COUNT; ++slot) {
    disconnectSlot(slot);
  }
}

void BambuMqttService::resetSlotRuntime(size_t slot, const BambuConfig& config) {
  if (slot >= BambuConfigLimits::PRINTER_COUNT) return;
  MqttConn& conn = conns_[slot];
  conn.lastMqttAttemptMs = 0U;
  conn.connectTimeMs = 0U;
  conn.mqttAttempted = false;
  conn.tokenRejected = false;
  conn.initialPushallPending = false;
  conn.consecutiveFails = 0U;
  conn.pushallSequence = 1U;
  conn.status = BambuMqttStatus{};
  conn.status.configured = configuredForMqtt(config) && slot < config.printerCount;
  conn.status.tokenSet = !config.accessToken.empty();
  conn.status.session = !config.enabled
                            ? BambuSessionState::INTEGRATION_DISABLED
                            : (conn.status.configured ? BambuSessionState::MQTT_CONNECTING
                                                      : BambuSessionState::UNCONFIGURED);
}

void BambuMqttService::setSlotConnectivity(size_t slot, bool connected) {
  if (!mutex_ || slot >= BambuConfigLimits::PRINTER_COUNT) return;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    states_[slot].connected = connected;
    conns_[slot].status.mqttConnected = connected;
    xSemaphoreGive(mutex_);
  }
}

void BambuMqttService::setSlotSession(size_t slot, BambuSessionState session, int mqttRc) {
  if (!mutex_ || slot >= BambuConfigLimits::PRINTER_COUNT) return;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    conns_[slot].status.session = session;
    if (mqttRc != -1) conns_[slot].status.lastMqttRc = mqttRc;
    conns_[slot].status.configured = configuredForMqtt(config_) && slot < config_.printerCount;
    conns_[slot].status.tokenSet = !config_.accessToken.empty();
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
