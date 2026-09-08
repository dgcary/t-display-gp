from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
pio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
mqtt = (ROOT / "src/network/BambuMqttService.cpp").read_text(encoding="utf-8")

errors = []

# Stage-2 diagnosis changes exactly one variable: PubSubClient's CONNACK/socket
# timeout. If the watchdog still fires near 16-17 s, the block is below
# PubSubClient's CONNACK wait (most likely WiFiClientSecure::connect). If it
# returns rc=-4 near 5 s instead, the no-yield CONNACK wait was the watchdog path.
if "-DMQTT_SOCKET_TIMEOUT=5" not in pio:
    errors.append("lilygo-t-display-s3 must compile PubSubClient with MQTT_SOCKET_TIMEOUT=5")

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

# Security and transport policy remain unchanged during this A/B test.
for required in (
    "setCACertBundle",
    "rootca_crt_bundle_start",
    "BAMBU_MQTT_KEEPALIVE_SEC = 30U",
    "BAMBU_MQTT_RECONNECT_MS = 30000U",
):
    if required not in mqtt:
        errors.append(f"Bambu MQTT missing unchanged policy marker: {required}")

if "setInsecure" in mqtt:
    errors.append("Bambu MQTT must not disable TLS verification")

if errors:
    for error in errors:
        print(f"ERROR: {error}")
    sys.exit(1)

print("Bambu PubSubClient 5s CONNACK-timeout A/B diagnostic contract: OK")
