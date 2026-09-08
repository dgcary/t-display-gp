from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
pio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
mqtt = (ROOT / "src/network/BambuMqttService.cpp").read_text(encoding="utf-8")

errors = []

# Stage-4 A/B diagnosis: physical testing proved the explicit strict-CA TLS
# connection can return successfully, but mqtt_->connect() can still starve
# CPU0 until the task watchdog fires. PubSubClient 2.8 performs a tight
# while (!_client->available()) CONNACK wait with no delay/yield before its
# socket timeout. Reduce only that timeout to 3 seconds so it expires with
# rc=-4 before the watchdog window. This is diagnostic, not the final fix.
for required in (
    "-DMQTT_SOCKET_TIMEOUT=3",
    "mqtt_real_tls_begin",
    "mqtt_real_tls_ok",
    "mqtt_real_tls_fail",
    "tls_->connect(broker, BAMBU_MQTT_PORT, BAMBU_REAL_TLS_TIMEOUT_MS)",
    "BAMBU_REAL_TLS_TIMEOUT_MS = 5000",
):
    haystack = pio if required.startswith("-D") else mqtt
    if required not in haystack:
        errors.append(f"missing stage-4 CONNACK boundary marker: {required}")

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

print("Bambu single-real-TLS + 3s PubSubClient CONNACK boundary diagnostic: OK")
