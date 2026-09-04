from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
PROTOCOL_HEADER = ROOT / "src" / "network" / "BambuCloudProtocol.h"
PROTOCOL_SOURCE = ROOT / "src" / "network" / "BambuCloudProtocol.cpp"
STORE_HEADER = ROOT / "src" / "network" / "BambuConfigStore.h"
STORE_SOURCE = ROOT / "src" / "network" / "BambuConfigStore.cpp"
CLIENT_HEADER = ROOT / "src" / "network" / "BambuCloudClient.h"
CLIENT_SOURCE = ROOT / "src" / "network" / "BambuCloudClient.cpp"
MQTT_HEADER = ROOT / "src" / "network" / "BambuMqttService.h"
MQTT_SOURCE = ROOT / "src" / "network" / "BambuMqttService.cpp"
BUILD_CONFIG = ROOT / "include" / "build_config.h"
PLATFORMIO = ROOT / "platformio.ini"

errors = []

for path in (PROTOCOL_HEADER, PROTOCOL_SOURCE):
    if not path.exists():
        errors.append(f"missing {path.relative_to(ROOT)}")

if PROTOCOL_HEADER.exists() and PROTOCOL_SOURCE.exists():
    header = PROTOCOL_HEADER.read_text(encoding="utf-8")
    source = PROTOCOL_SOURCE.read_text(encoding="utf-8")

    required_header_markers = [
        "enum class BambuLoginDisposition",
        "TOKEN",
        "NEED_EMAIL_CODE",
        "NEED_TFA",
        "ERROR",
        "parseBambuLoginReply",
        "extractBambuUserIdFromJwt",
        "parseBambuProfileUserId",
        "parseBambuDeviceList",
        "bambuReportTopic",
    ]
    for marker in required_header_markers:
        if marker not in header:
            errors.append(f"BambuCloudProtocol.h missing marker: {marker}")

    forbidden = ["HTTPClient", "WiFiClientSecure", "setInsecure", "NetworkArbiter"]
    joined = header + "\n" + source
    for marker in forbidden:
        if marker in joined:
            errors.append(f"pure protocol layer must not depend on transport: {marker}")

for path in (STORE_HEADER, STORE_SOURCE, CLIENT_HEADER, CLIENT_SOURCE, BUILD_CONFIG, PLATFORMIO,
             MQTT_HEADER, MQTT_SOURCE):
    if not path.exists():
        errors.append(f"missing {path.relative_to(ROOT)}")

if STORE_HEADER.exists() and STORE_SOURCE.exists():
    store_header = STORE_HEADER.read_text(encoding="utf-8")
    store_source = STORE_SOURCE.read_text(encoding="utf-8")
    for marker in ("class BambuConfigStore", "load", "save", "clear"):
        if marker not in store_header:
            errors.append(f"BambuConfigStore.h missing marker: {marker}")
    for marker in ("Preferences", "BAMBU_CONFIG_NAMESPACE", "BAMBU_CONFIG_KEY", "BambuConfigCodec"):
        if marker not in store_source:
            errors.append(f"BambuConfigStore.cpp missing marker: {marker}")
    if "Serial" in store_source:
        errors.append("BambuConfigStore.cpp must not log persisted secret material")

if CLIENT_HEADER.exists() and CLIENT_SOURCE.exists():
    client_header = CLIENT_HEADER.read_text(encoding="utf-8")
    client_source = CLIENT_SOURCE.read_text(encoding="utf-8")
    for marker in (
        "enum class BambuCloudError",
        "class BambuCloudClient",
        "login(",
        "fetchUserId(",
        "fetchPrinters(",
    ):
        if marker not in client_header:
            errors.append(f"BambuCloudClient.h missing marker: {marker}")

    for marker in (
        "WiFiClientSecure",
        "HTTPClient",
        "setCACertBundle",
        "rootca_crt_bundle_start",
        "sharedNetworkArbiter",
        "BAMBU_HTTPS_MAX_BODY_BYTES",
    ):
        if marker not in client_source:
            errors.append(f"BambuCloudClient.cpp missing verified transport marker: {marker}")

    if "setInsecure" in client_source:
        errors.append("Bambu Cloud credential transport must never call setInsecure()")
    if "Serial" in client_source:
        errors.append("BambuCloudClient.cpp must not log credentials or cloud response bodies")

if MQTT_HEADER.exists() and MQTT_SOURCE.exists():
    mqtt_header = MQTT_HEADER.read_text(encoding="utf-8")
    mqtt_source = MQTT_SOURCE.read_text(encoding="utf-8")
    for marker in ("class BambuMqttService", "begin(", "snapshot()", "status()"):
        if marker not in mqtt_header:
            errors.append(f"BambuMqttService.h missing marker: {marker}")
    for marker in (
        "PubSubClient",
        "WiFiClientSecure",
        "setCACertBundle",
        "rootca_crt_bundle_start",
        "setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)",
        "sharedNetworkArbiter",
        "bambuBrokerForRegion",
        "bambuReportTopic",
        "device/",
        "/request",
        "pushall",
        "mqtt.loop()",
    ):
        if marker not in mqtt_source:
            errors.append(f"BambuMqttService.cpp missing MQTT marker: {marker}")
    if "setInsecure" in mqtt_source:
        errors.append("Bambu MQTT Cloud transport must never call setInsecure()")

if BUILD_CONFIG.exists():
    build_config = BUILD_CONFIG.read_text(encoding="utf-8")
    required_build_markers = [
        'BAMBU_CONFIG_NAMESPACE[] = "bambucloud"',
        'BAMBU_CONFIG_KEY[] = "config"',
        "BAMBU_HTTPS_MAX_BODY_BYTES",
        "BAMBU_MQTT_BUFFER_BYTES",
    ]
    for marker in required_build_markers:
        if marker not in build_config:
            errors.append(f"build_config.h missing Bambu marker: {marker}")
    if "BAMBU_MQTT_BUFFER_BYTES = 40960" not in build_config:
        errors.append("BAMBU_MQTT_BUFFER_BYTES must be at least the approved 40960-byte V1 buffer")

if PLATFORMIO.exists():
    platformio = PLATFORMIO.read_text(encoding="utf-8")
    if "knolleary/PubSubClient" not in platformio:
        errors.append("platformio.ini missing PubSubClient dependency")

# No Bambu-owned production source may weaken TLS verification.
for path in sorted((ROOT / "src" / "network").glob("Bambu*.cpp")):
    text = path.read_text(encoding="utf-8")
    if "setInsecure" in text:
        errors.append(f"{path.relative_to(ROOT)} contains forbidden setInsecure()")

if errors:
    for error in errors:
        print(f"ERROR: {error}")
    sys.exit(1)

print("Bambu Cloud protocol + secret store + verified HTTPS + persistent MQTT contract: OK")
