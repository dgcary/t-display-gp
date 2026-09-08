from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
pio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
mqtt = (ROOT / "src/network/BambuMqttService.cpp").read_text(encoding="utf-8")

errors = []

# Stage-3 diagnosis/fix: use one real strict-CA TLS socket for MQTT, bounded to
# 5 seconds, then let PubSubClient reuse that already-connected Client. The
# previous extra TLS preflight must not run immediately before the real MQTT
# connection because physical testing showed the second TLS path can block long
# enough to starve CPU0 IDLE and trigger the task watchdog.
for required in (
    "-DMQTT_SOCKET_TIMEOUT=5",
    "mqtt_real_tls_begin",
    "mqtt_real_tls_ok",
    "mqtt_real_tls_fail",
    "tls_->connect(broker, BAMBU_MQTT_PORT, BAMBU_REAL_TLS_TIMEOUT_MS)",
    "BAMBU_REAL_TLS_TIMEOUT_MS = 5000",
):
    haystack = pio if required.startswith("-D") else mqtt
    if required not in haystack:
        errors.append(f"missing single-real-TLS marker: {required}")

if "runLayeredConnectionProbe(broker);" in mqtt:
    errors.append("real MQTT path must not perform the old extra TLS preflight before MQTT")

# The real TLS client must be connected before PubSubClient::connect so
# PubSubClient sees Client::connected()==true and does not open a second TLS
# socket internally.
real_tls_pos = mqtt.find("tls_->connect(broker, BAMBU_MQTT_PORT, BAMBU_REAL_TLS_TIMEOUT_MS)")
pubsub_connect_pos = mqtt.find("mqtt_->connect(clientId")
if real_tls_pos == -1 or pubsub_connect_pos == -1 or real_tls_pos > pubsub_connect_pos:
    errors.append("real TLS connect must occur before PubSubClient MQTT connect")

# Do not hide the bug by disabling/deleting/resetting watchdogs.
for forbidden in (
    "disableCore0WDT",
    "disableCore1WDT",
    "disableLoopWDT",
    "esp_task_wdt_delete",
    "esp_task_wdt_reset",
):
    if forbidden in mqtt:
        errors.append(f"Bambu MQTT contains forbidden watchdog workaround: {forbidden}")

# Security and MQTT policy stay unchanged.
for required in (
    "setCACertBundle",
    "rootca_crt_bundle_start",
    "BAMBU_MQTT_KEEPALIVE_SEC = 30U",
    "BAMBU_MQTT_RECONNECT_MS = 30000U",
    "setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)",
):
    if required not in mqtt:
        errors.append(f"Bambu MQTT missing unchanged policy marker: {required}")

if "setInsecure" in mqtt:
    errors.append("Bambu MQTT must not disable TLS verification")

if errors:
    for error in errors:
        print(f"ERROR: {error}")
    sys.exit(1)

print("Bambu single-real-TLS + 5s PubSubClient timeout watchdog contract: OK")
