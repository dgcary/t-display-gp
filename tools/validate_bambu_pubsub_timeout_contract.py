from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
pio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
mqtt = (ROOT / "src/network/BambuMqttService.cpp").read_text(encoding="utf-8")
header = (ROOT / "src/network/BambuMqttService.h").read_text(encoding="utf-8")
main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")

errors = []

# Keep the proven BambuHelper runtime baseline.
if "platform = espressif32@6.12.0" not in pio:
    errors.append("ESP32 runtime must remain aligned to espressif32@6.12.0 / Arduino-ESP32 2.0.17")
if "MQTT_SOCKET_TIMEOUT" in pio:
    errors.append("project must not override PubSubClient MQTT_SOCKET_TIMEOUT")
if "bambuMqttService.process(nowMs);" not in main:
    errors.append("main loop must drive BambuMqttService::process(nowMs)")

# High-fidelity multi-printer model: one persistent TLS/MQTT context and one
# cached state per configured printer. Switching the visible/active slot must
# not tear down the network connection.
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
        errors.append(f"single-active-printer runtime state must be retired: {forbidden}")

for required in (
    "for (size_t slot = 0; slot < config.printerCount; ++slot)",
    "MqttConn& conn = conns_[slot]",
    "conn.tls = new (std::nothrow) WiFiClientSecure()",
    "conn.mqtt = new (std::nothrow) PubSubClient(*conn.tls)",
    "conn.tls->setCACertBundle(rootca_crt_bundle_start)",
    "conn.tls->setTimeout(15)",
    "conn.mqtt->setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)",
    "conn.mqtt->setKeepAlive(BAMBU_MQTT_KEEPALIVE_SEC)",
    "findSlotForTopic(topic)",
    "states_[slot]",
):
    if required not in mqtt:
        errors.append(f"missing per-slot BambuHelper-aligned MQTT marker: {required}")

# Active-printer switching is now a local UI/NVS operation. It may persist the
# active index, but must not explicitly disconnect Cloud MQTT or clear all slot
# state. Extract the function body loosely so comments elsewhere do not matter.
match = re.search(r"bool BambuMqttService::cycleActivePrinter\(int direction\)\s*\{(.*?)\n\}", mqtt, re.S)
if not match:
    errors.append("cycleActivePrinter implementation missing")
else:
    body = match.group(1)
    for forbidden in ("disconnectMqtt", "disconnectSlot", "states_ =", "BambuState{}"):
        if forbidden in body:
            errors.append(f"active printer switch must not tear down persistent slots: {forbidden}")

# Per-slot reconnect cleanup must exist for failed/stale connections, while a
# successful connection remains alive when another printer becomes active.
for required in (
    "delete conn.mqtt",
    "conn.mqtt = nullptr",
    "conn.tls->stop()",
    "delete conn.tls",
    "conn.tls = nullptr",
):
    if required not in mqtt:
        errors.append(f"per-slot reconnect cleanup missing: {required}")

# Preserve known-good runtime/security boundaries.
for forbidden in (
    "xTaskCreatePinnedToCore",
    '"bambu-mqtt"',
    "taskThunk",
    "taskLoop",
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
    "esp_task_wdt_reset",
):
    if required not in mqtt:
        errors.append(f"missing retained BambuHelper runtime marker: {required}")

for forbidden in ("pause", "resume", "stop_print", "ledctrl", "temperature"):
    if f'"{forbidden}"' in mqtt:
        errors.append(f"Bambu MQTT must remain read-only; found control command: {forbidden}")

if errors:
    for error in errors:
        print(f"ERROR: {error}")
    sys.exit(1)

print("BambuHelper-aligned persistent multi-printer MQTT runtime contract: OK")
