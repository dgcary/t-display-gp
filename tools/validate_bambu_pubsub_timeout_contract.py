from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
pio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
mqtt = (ROOT / "src/network/BambuMqttService.cpp").read_text(encoding="utf-8")
header = (ROOT / "src/network/BambuMqttService.h").read_text(encoding="utf-8")
main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")

errors = []

# High-fidelity runtime alignment with Keralots/BambuHelper's proven Cloud MQTT
# execution model. Physical Stage-4 evidence confirmed that the old CPU0-pinned
# service task could starve IDLE while PubSubClient waited for CONNACK. The
# production fix is to align the framework/runtime lifecycle rather than keep
# shrinking PubSubClient timeouts.
if "platform = espressif32@6.12.0" not in pio:
    errors.append("ESP32 runtime must align to espressif32@6.12.0 / Arduino-ESP32 2.0.17")

if "MQTT_SOCKET_TIMEOUT" in pio:
    errors.append("project must not override PubSubClient MQTT_SOCKET_TIMEOUT in the aligned runtime")

for required in (
    "void process(uint32_t nowMs);",
):
    if required not in header:
        errors.append(f"BambuMqttService header missing loop-driven API: {required}")

for forbidden in (
    "taskThunk",
    "taskLoop",
    "TaskHandle_t task_",
):
    if forbidden in header:
        errors.append(f"BambuMqttService must not retain dedicated task API/state: {forbidden}")

for forbidden in (
    "xTaskCreatePinnedToCore",
    '"bambu-mqtt"',
    "void BambuMqttService::taskLoop()",
    "void BambuMqttService::taskThunk",
):
    if forbidden in mqtt:
        errors.append(f"Bambu MQTT must be main-loop driven, found dedicated-task marker: {forbidden}")

if "bambuMqttService.process(nowMs);" not in main:
    errors.append("main loop must call bambuMqttService.process(nowMs)")

# Reference Cloud connection path: PubSubClient owns the TLS connect using a
# freshly allocated WiFiClientSecure with strict CA verification.
for required in (
    "setCACertBundle(rootca_crt_bundle_start)",
    "tls_->setTimeout(15)",
    "new (std::nothrow) PubSubClient(*tls_)",
    "setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)",
    "BAMBU_MQTT_KEEPALIVE_SEC = 30U",
    '"bblp_%08',
    "mqtt_->connect(clientId, config.cloudUserId.c_str(), config.accessToken.c_str())",
):
    if required not in mqtt:
        errors.append(f"missing BambuHelper-aligned Cloud MQTT marker: {required}")

# The old diagnostic pre-connect path must not remain in the normal connection
# engine. PubSubClient should see a fresh, not-yet-connected TLS Client and own
# the TCP/TLS handshake just as upstream BambuHelper does.
for forbidden in (
    "BAMBU_REAL_TLS_TIMEOUT_MS",
    "mqtt_real_tls_begin",
    "mqtt_real_tls_ok",
    "mqtt_real_tls_fail",
    "tls_->connect(broker, BAMBU_MQTT_PORT",
):
    if forbidden in mqtt:
        errors.append(f"remove Stage-2/3/4 explicit TLS diagnostic path: {forbidden}")

if "runLayeredConnectionProbe(broker);" in mqtt:
    errors.append("normal MQTT reconnect must not run layered diagnostic preflight")

# BambuHelper-aligned Cloud backoff and delayed initial pushall.
for required in (
    "BAMBU_CLOUD_RECONNECT_BASE_MS = 30000U",
    "BAMBU_CLOUD_RECONNECT_PHASE2_MS = 60000U",
    "BAMBU_CLOUD_RECONNECT_PHASE3_MS = 120000U",
    "BAMBU_BACKOFF_PHASE1_FAILS = 5U",
    "BAMBU_BACKOFF_PHASE3_FAILS = 15U",
    "BAMBU_PUSHALL_INITIAL_DELAY_MS = 2000U",
    "consecutiveFails_",
    "initialPushallPending_",
):
    haystack = header + "\n" + mqtt
    if required not in haystack:
        errors.append(f"missing reconnect/pushall lifecycle marker: {required}")

# Client objects must be destroyed between Cloud attempts so stale TLS/socket
# state cannot leak into the next reconnect.
for required in (
    "delete mqtt_",
    "mqtt_ = nullptr",
    "tls_->stop()",
    "delete tls_",
    "tls_ = nullptr",
):
    if required not in mqtt:
        errors.append(f"Bambu MQTT reconnect cleanup missing: {required}")

# The upstream reference resets the task watchdog around long operations but
# never disables watchdog protection. Reset calls are permitted; disabling or
# deleting watchdogs remains forbidden.
for forbidden in (
    "disableCore0WDT",
    "disableCore1WDT",
    "disableLoopWDT",
    "esp_task_wdt_delete",
):
    if forbidden in mqtt:
        errors.append(f"Bambu MQTT contains forbidden watchdog disable/delete: {forbidden}")

if "esp_task_wdt_reset" not in mqtt:
    errors.append("aligned runtime should retain BambuHelper-style watchdog reset calls around long MQTT work")

# Security/read-only policy remains unchanged.
if "setInsecure" in mqtt:
    errors.append("Bambu Cloud MQTT must not disable TLS verification")

for forbidden in (
    "pause",
    "resume",
    "stop_print",
    "ledctrl",
    "temperature",
):
    # Only guard against command payload strings in the MQTT service, not words
    # in comments or unrelated types.
    if f'"{forbidden}"' in mqtt:
        errors.append(f"Bambu MQTT must remain read-only; found control command: {forbidden}")

if errors:
    for error in errors:
        print(f"ERROR: {error}")
    sys.exit(1)

print("BambuHelper-aligned loop-driven Cloud MQTT runtime contract: OK")
