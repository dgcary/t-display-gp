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
PORTAL_HEADER = ROOT / "src" / "network" / "IntegrationConfigPortal.h"
PORTAL_SOURCE = ROOT / "src" / "network" / "IntegrationConfigPortal.cpp"
PORTAL_MODEL_HEADER = ROOT / "src" / "network" / "BambuPortalModel.h"
PORTAL_MODEL_SOURCE = ROOT / "src" / "network" / "BambuPortalModel.cpp"
APP_HEADER = ROOT / "src" / "app" / "BambuApp.h"
APP_SOURCE = ROOT / "src" / "app" / "BambuApp.cpp"
SCREEN_HEADER = ROOT / "src" / "ui" / "BambuScreen.h"
SCREEN_SOURCE = ROOT / "src" / "ui" / "BambuScreen.cpp"
APP_SHELL = ROOT / "src" / "app" / "AppShell.h"
MAIN = ROOT / "src" / "main.cpp"
BUILD_CONFIG = ROOT / "include" / "build_config.h"
PLATFORMIO = ROOT / "platformio.ini"
NOTICES = ROOT / "THIRD_PARTY_NOTICES.md"

errors = []
required_files = (
    PROTOCOL_HEADER, PROTOCOL_SOURCE, STORE_HEADER, STORE_SOURCE,
    CLIENT_HEADER, CLIENT_SOURCE, MQTT_HEADER, MQTT_SOURCE,
    PORTAL_HEADER, PORTAL_SOURCE, PORTAL_MODEL_HEADER, PORTAL_MODEL_SOURCE,
    APP_HEADER, APP_SOURCE, SCREEN_HEADER, SCREEN_SOURCE,
    APP_SHELL, MAIN, BUILD_CONFIG, PLATFORMIO, NOTICES,
)
for path in required_files:
    if not path.exists():
        errors.append(f"missing {path.relative_to(ROOT)}")

if PROTOCOL_HEADER.exists() and PROTOCOL_SOURCE.exists():
    header = PROTOCOL_HEADER.read_text(encoding="utf-8")
    source = PROTOCOL_SOURCE.read_text(encoding="utf-8")
    for marker in (
        "enum class BambuLoginDisposition", "TOKEN", "NEED_EMAIL_CODE", "NEED_TFA", "ERROR",
        "parseBambuLoginReply", "extractBambuUserIdFromJwt", "parseBambuProfileUserId",
        "parseBambuDeviceList", "bambuReportTopic",
    ):
        if marker not in header:
            errors.append(f"BambuCloudProtocol.h missing marker: {marker}")
    for marker in ("HTTPClient", "WiFiClientSecure", "setInsecure", "NetworkArbiter"):
        if marker in header + "\n" + source:
            errors.append(f"pure protocol layer must not depend on transport: {marker}")

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
    for marker in ("enum class BambuCloudError", "class BambuCloudClient", "login(", "fetchUserId(", "fetchPrinters("):
        if marker not in client_header:
            errors.append(f"BambuCloudClient.h missing marker: {marker}")
    for marker in (
        "WiFiClientSecure", "HTTPClient", "setCACertBundle", "rootca_crt_bundle_start",
        "sharedNetworkArbiter", "BAMBU_HTTPS_MAX_BODY_BYTES",
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
        "PubSubClient", "WiFiClientSecure", "setCACertBundle", "rootca_crt_bundle_start",
        "setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)", "sharedNetworkArbiter",
        "bambuBrokerForRegion", "bambuReportTopic", "device/", "/request", "pushall", "mqtt_->loop()",
    ):
        if marker not in mqtt_source:
            errors.append(f"BambuMqttService.cpp missing MQTT marker: {marker}")
    if "setInsecure" in mqtt_source:
        errors.append("Bambu MQTT Cloud transport must never call setInsecure()")
    if mqtt_source.count("mqtt_->publish") != 1 or '"pushall"' not in mqtt_source:
        errors.append("Bambu MQTT V1 may publish only the single read-only pushall request path")
    for control in ('"pause"', '"resume"', '"stop"', '"temperature"'):
        if control in mqtt_source:
            errors.append(f"Bambu MQTT V1 contains forbidden printer-control command: {control}")

if APP_HEADER.exists() and APP_SOURCE.exists() and SCREEN_SOURCE.exists() and APP_SHELL.exists() and MAIN.exists():
    app_header = APP_HEADER.read_text(encoding="utf-8")
    app_source = APP_SOURCE.read_text(encoding="utf-8")
    screen_source = SCREEN_SOURCE.read_text(encoding="utf-8")
    shell = APP_SHELL.read_text(encoding="utf-8")
    main = MAIN.read_text(encoding="utf-8")
    for marker in ("AppId::BAMBU", 'return "Bambu Lab"', "BambuMqttService&"):
        if marker not in app_header:
            errors.append(f"BambuApp.h missing marker: {marker}")
    if "BAMBU" not in shell:
        errors.append("AppShell.h missing AppId::BAMBU")
    for marker in ('{AppId::BAMBU, "Bambu Lab"}', "&bambuApp", 'case AppId::BAMBU: return "BAMBU";'):
        if marker not in main:
            errors.append(f"main.cpp missing Bambu wiring marker: {marker}")
    for forbidden in ("connectMqtt", "disconnectMqtt", "publish(", "mqtt_->", "pause", "resume", "stopPrint"):
        if forbidden in app_source or forbidden in screen_source:
            errors.append(f"Bambu app/screen must remain passive and read-only: {forbidden}")

if PORTAL_HEADER.exists() and PORTAL_SOURCE.exists() and PORTAL_MODEL_SOURCE.exists():
    portal_header = PORTAL_HEADER.read_text(encoding="utf-8")
    portal_source = PORTAL_SOURCE.read_text(encoding="utf-8")
    portal_model = PORTAL_MODEL_SOURCE.read_text(encoding="utf-8")
    for marker in (
        "class IntegrationConfigPortal", "WebServer server{8081}",
        '"/api/ha/status"', '"/api/ha/config"', '"/api/bambu/status"',
        '"/api/bambu/login"', '"/api/bambu/printers"', '"/api/bambu/config"', '"/api/bambu/logout"',
        'doc["password_set"]', 'doc["token_set"]', "clearBambuPortalCredentials",
    ):
        joined = portal_header + "\n" + portal_source + "\n" + portal_model
        if marker not in joined:
            errors.append(f"integrations portal missing marker: {marker}")
    for forbidden in ('doc["password"]', 'doc["accessToken"]', 'doc["access_token"]', 'doc["token"]'):
        if forbidden in portal_source:
            errors.append(f"Bambu status/config response may expose a secret key: {forbidden}")
    if "setInsecure" in portal_source:
        errors.append("Integrations portal must not weaken Bambu TLS verification")
    if (ROOT / "src" / "network" / "HomeAssistantConfigPortal.cpp").exists() or (ROOT / "src" / "network" / "HomeAssistantConfigPortal.h").exists():
        errors.append("HA-only portal must be retired; port 8081 is owned by IntegrationConfigPortal")

if BUILD_CONFIG.exists():
    build_config = BUILD_CONFIG.read_text(encoding="utf-8")
    for marker in (
        'BAMBU_CONFIG_NAMESPACE[] = "bambucloud"', 'BAMBU_CONFIG_KEY[] = "config"',
        "BAMBU_HTTPS_MAX_BODY_BYTES", "BAMBU_MQTT_BUFFER_BYTES",
    ):
        if marker not in build_config:
            errors.append(f"build_config.h missing Bambu marker: {marker}")
    if "BAMBU_MQTT_BUFFER_BYTES = 40960" not in build_config:
        errors.append("BAMBU_MQTT_BUFFER_BYTES must be at least the approved 40960-byte V1 buffer")

if PLATFORMIO.exists():
    platformio = PLATFORMIO.read_text(encoding="utf-8")
    if "knolleary/PubSubClient" not in platformio:
        errors.append("platformio.ini missing PubSubClient dependency")
    if "+<network/BambuPortalModel.cpp>" not in platformio:
        errors.append("native suite must compile BambuPortalModel.cpp")

if NOTICES.exists():
    notices = NOTICES.read_text(encoding="utf-8")
    for marker in ("Keralots/BambuHelper", "knolleary/PubSubClient"):
        if marker not in notices:
            errors.append(f"THIRD_PARTY_NOTICES.md missing {marker}")

for path in sorted((ROOT / "src" / "network").glob("Bambu*.cpp")):
    text = path.read_text(encoding="utf-8")
    if "setInsecure" in text:
        errors.append(f"{path.relative_to(ROOT)} contains forbidden setInsecure()")

if errors:
    for error in errors:
        print(f"ERROR: {error}")
    sys.exit(1)

print("Bambu Cloud auth + persistent MQTT + read-only app + unified portal security contract: OK")
