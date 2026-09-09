from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
pio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
mqtt = (ROOT / "src/network/BambuMqttService.cpp").read_text(encoding="utf-8")
header = (ROOT / "src/network/BambuMqttService.h").read_text(encoding="utf-8")
main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
build_config = (ROOT / "include/build_config.h").read_text(encoding="utf-8")

errors = []

if "platform = espressif32@6.12.0" not in pio:
    errors.append("ESP32 runtime must remain aligned to espressif32@6.12.0 / Arduino-ESP32 2.0.17")
if "MQTT_SOCKET_TIMEOUT" in pio:
    errors.append("project must not override PubSubClient MQTT_SOCKET_TIMEOUT")

# Real Cloud connect/TLS must never execute synchronously from the Arduino UI loop.
if "bambuMqttService.process(nowMs);" in main:
    errors.append("Arduino loop must not synchronously drive blocking Bambu MQTT connect work")
for required in (
    "TaskHandle_t task_ = nullptr;",
    "static void taskThunk(void* arg);",
    "void taskLoop();",
):
    if required not in header:
        errors.append(f"missing background Bambu MQTT worker marker: {required}")
for required in (
    "xTaskCreatePinnedToCore",
    '"bambu-mqtt"',
    "BambuMqttService::taskThunk",
    "BambuMqttService::taskLoop",
):
    if required not in mqtt:
        errors.append(f"missing background Bambu MQTT worker implementation: {required}")

# Multi-printer model remains: one persistent TLS/MQTT context and cache per slot.
for required in (
    "struct MqttConn",
    "std::array<MqttConn, BambuConfigLimits::PRINTER_COUNT> conns_",
    "std::array<BambuState, BambuConfigLimits::PRINTER_COUNT> states_",
    "bool connectSlot(size_t slot, uint32_t nowMs);",
    "void disconnectSlot(size_t slot);",
    "bool publishInitialPushall(size_t slot);",
    "size_t findSlotForTopic(const char* topic) const;",
):
    if required not in header:
        errors.append(f"missing persistent multi-printer runtime marker: {required}")

for forbidden in (
    "WiFiClientSecure* tls_ = nullptr",
    "PubSubClient* mqtt_ = nullptr",
    "BambuState state_;",
    "uint32_t lastMqttAttemptMs_",
    "uint16_t consecutiveFails_",
):
    if forbidden in header:
        errors.append(f"single-active-printer runtime state must remain retired: {forbidden}")

for required in (
    "for (size_t slot = 0; slot < config.printerCount; ++slot)",
    "MqttConn& conn = conns_[slot]",
    "conn.tls = new (std::nothrow) WiFiClientSecure()",
    "conn.mqtt = new (std::nothrow) PubSubClient(*conn.tls)",
    "conn.tls->setCACertBundle(rootca_crt_bundle_start)",
    "conn.mqtt->setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)",
    "conn.mqtt->setKeepAlive(BAMBU_MQTT_KEEPALIVE_SEC)",
    "findSlotForTopic(topic)",
    "states_[slot]",
):
    if required not in mqtt:
        errors.append(f"missing per-slot persistent MQTT marker: {required}")

# Arduino-ESP32 has separate TCP/socket and TLS-handshake timeouts. Both must be
# explicitly bounded so a failed Cloud handshake cannot fall back to the core's
# ~120 s default handshake timeout.
if "BAMBU_MQTT_CONNECT_TIMEOUT_SEC = 5U" not in build_config:
    errors.append("Bambu MQTT connect phase timeout must be explicitly fixed at 5 seconds")
for required in (
    "conn.tls->setTimeout(BuildConfig::BAMBU_MQTT_CONNECT_TIMEOUT_SEC)",
    "conn.tls->setHandshakeTimeout(BuildConfig::BAMBU_MQTT_CONNECT_TIMEOUT_SEC)",
):
    if required not in mqtt:
        errors.append(f"missing bounded Bambu TCP/TLS timeout: {required}")

# Backoff starts when a failed blocking call RETURNS, not when it started. If the
# start timestamp is used, a long failure consumes the whole retry interval and
# causes the observed immediate reconnect loop.
if "conn.lastMqttAttemptMs = nowMs;" in mqtt:
    errors.append("retry anchor must not be captured before the blocking connect call")
if "conn.lastMqttAttemptMs = millis();" not in mqtt:
    errors.append("retry anchor must be recorded from failure completion time")

# Active-printer switching stays local and must not tear down sibling sockets.
match = re.search(r"bool BambuMqttService::cycleActivePrinter\(int direction\)\s*\{(.*?)\n\}", mqtt, re.S)
if not match:
    errors.append("cycleActivePrinter implementation missing")
else:
    body = match.group(1)
    for forbidden in ("disconnectMqtt", "disconnectSlot", "states_ =", "BambuState{}"):
        if forbidden in body:
            errors.append(f"active printer switch must not tear down persistent slots: {forbidden}")

for required in (
    "delete conn.mqtt",
    "conn.mqtt = nullptr",
    "conn.tls->stop()",
    "delete conn.tls",
    "conn.tls = nullptr",
):
    if required not in mqtt:
        errors.append(f"per-slot reconnect cleanup missing: {required}")

for forbidden in (
    "setInsecure",
    "disableCore0WDT",
    "disableCore1WDT",
    "disableLoopWDT",
    "esp_task_wdt_delete",
    "mqtt_real_tls_begin",
    "mqtt_real_tls_ok",
    "mqtt_real_tls_fail",
):
    if forbidden in mqtt + "\n" + header:
        errors.append(f"forbidden legacy/insecure runtime marker: {forbidden}")

for required in (
    "BAMBU_CLOUD_RECONNECT_BASE_MS = 30000U",
    "BAMBU_CLOUD_RECONNECT_PHASE2_MS = 60000U",
    "BAMBU_CLOUD_RECONNECT_PHASE3_MS = 120000U",
    "BAMBU_PUSHALL_INITIAL_DELAY_MS = 2000U",
    "BAMBU_MQTT_KEEPALIVE_SEC = 30U",
    '"bblp_%08',
):
    if required not in mqtt:
        errors.append(f"missing retained Bambu runtime marker: {required}")

for forbidden in ("pause", "resume", "stop_print", "ledctrl", "temperature"):
    if f'"{forbidden}"' in mqtt:
        errors.append(f"Bambu MQTT must remain read-only; found control command: {forbidden}")

if errors:
    for error in errors:
        print(f"ERROR: {error}")
    sys.exit(1)

print("Bambu background-worker + bounded-connect + completion-anchored backoff contract: OK")
