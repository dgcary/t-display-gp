from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
paths = {
    "config_h": ROOT / "src/network/BambuConfig.h",
    "config_cpp": ROOT / "src/network/BambuConfig.cpp",
    "store_cpp": ROOT / "src/network/BambuConfigStore.cpp",
    "client_h": ROOT / "src/network/BambuCloudClient.h",
    "client_cpp": ROOT / "src/network/BambuCloudClient.cpp",
    "mqtt_h": ROOT / "src/network/BambuMqttService.h",
    "mqtt_cpp": ROOT / "src/network/BambuMqttService.cpp",
    "portal_cpp": ROOT / "src/network/IntegrationConfigPortal.cpp",
    "portal_model_h": ROOT / "src/network/BambuPortalModel.h",
    "app_cpp": ROOT / "src/app/BambuApp.cpp",
    "screen_h": ROOT / "src/ui/BambuScreen.h",
    "screen_cpp": ROOT / "src/ui/BambuScreen.cpp",
    "main": ROOT / "src/main.cpp",
    "build": ROOT / "include/build_config.h",
    "pio": ROOT / "platformio.ini",
    "notices": ROOT / "THIRD_PARTY_NOTICES.md",
}
errors = []
for name, path in paths.items():
    if not path.exists():
        errors.append(f"missing {path.relative_to(ROOT)}")


def text(name):
    return paths[name].read_text(encoding="utf-8") if paths[name].exists() else ""


def require(name, markers):
    data = text(name)
    for marker in markers:
        if marker not in data:
            errors.append(f"{paths[name].name} missing marker: {marker}")


def forbid(name, markers):
    data = text(name)
    for marker in markers:
        if marker in data:
            errors.append(f"{paths[name].name} contains obsolete/forbidden marker: {marker}")


require("config_h", ["struct BambuPrinterConfig", "PRINTER_COUNT = 4", "printers", "printerCount", "activePrinterIndex", "activeBambuPrinter", "selectRelativeBambuPrinter"])
forbid("config_h", ["std::string email;", "std::string password;", "verificationCode", "tfaKey"])
require("config_cpp", ["CONFIG_SCHEMA_V2 = 2", "decodeV1", 'createNestedArray("printers")', 'doc["active_printer"]', "selectRelativeBambuPrinter"])
forbid("config_cpp", ['doc["email"]', 'doc["password"]'])
require("store_cpp", ["legacySchema", "save(parsed)"])
forbid("store_cpp", ["Serial"])

require("client_h", ["class BambuCloudClient", "fetchUserId(", "fetchPrinters("])
forbid("client_h", ["BambuCloudLoginResult", "BambuVerificationType", "login(", "submitVerificationCode(", "requestVerificationCode(", "submitTfaCode("])
require("client_cpp", ["WiFiClientSecure", "HTTPClient", "setCACertBundle", "rootca_crt_bundle_start", "sharedNetworkArbiter", '"/v1/user-service/my/profile"', '"/api/v1/iot-service/api/user/bind"', "extractBambuUserIdFromJwt"])
forbid("client_cpp", ["setInsecure", '"/v1/user-service/user/login"', "sendsmscode", "sendemail/code", '"/api/sign-in/tfa"', '"/api/csrf"', "Serial"])

require("mqtt_h", [
    "class BambuMqttService", "void process(uint32_t nowMs);", "snapshot()", "status()",
    "configSnapshot()", "replaceConfig(", "cycleActivePrinter(", "externalConfigRevision_",
    "struct MqttConn", "std::array<MqttConn, BambuConfigLimits::PRINTER_COUNT> conns_",
    "std::array<BambuState, BambuConfigLimits::PRINTER_COUNT> states_",
    "TaskHandle_t task_ = nullptr;", "static void taskThunk(void* arg);", "void taskLoop();",
])
forbid("mqtt_h", [
    "BambuCloudClient", "BambuSessionModel sessionModel_", "setPendingVerification",
    "submitVerificationCode", "requestVerificationCode", "pendingTfaKey", "passwordSet",
    "verificationRequired", "verificationType",
    "WiFiClientSecure* tls_ = nullptr", "PubSubClient* mqtt_ = nullptr", "BambuState state_;",
])
require("mqtt_cpp", [
    "PubSubClient", "WiFiClientSecure", "setCACertBundle", "rootca_crt_bundle_start",
    "conn.tls->setTimeout(BuildConfig::BAMBU_MQTT_CONNECT_TIMEOUT_SEC)",
    "conn.tls->setHandshakeTimeout(BuildConfig::BAMBU_MQTT_CONNECT_TIMEOUT_SEC)",
    "conn.mqtt->setSocketTimeout(BuildConfig::BAMBU_MQTT_CONNECT_TIMEOUT_SEC)",
    "conn.mqtt->setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)",
    "sharedNetworkArbiter", "selectRelativeBambuPrinter", "cycleActivePrinter(",
    "bambuBrokerForRegion", "bambuReportTopic", "pushall", "conn.mqtt->loop()",
    "externalConfigRevision_", "states_[slot] = BambuState{}", "conn.tokenRejected",
    "conn.consecutiveFails", "conn.initialPushallPending", "mqtt_connect_fail", "WiFi.RSSI()",
    "esp_get_free_heap_size()", "esp_task_wdt_reset", '"bblp_%08',
    "BAMBU_PUSHALL_INITIAL_DELAY_MS = 2000U", "BAMBU_CLOUD_RECONNECT_PHASE2_MS = 60000U",
    "BAMBU_CLOUD_RECONNECT_PHASE3_MS = 120000U", "findSlotForTopic(topic)",
    "xTaskCreatePinnedToCore", '"bambu-mqtt"', "BambuMqttService::taskLoop",
])
forbid("mqtt_cpp", [
    "setInsecure", "performRelogin", "setPendingVerification", "submitVerificationCode",
    "requestVerificationCode", "fetchPrinters(", "fetchUserId(", "VERIFICATION_REQUIRED",
    "result=CHALLENGE", "config.accessToken.c_str(), WiFi.RSSI",
    "mqtt_real_tls_begin", "mqtt_real_tls_ok", "mqtt_real_tls_fail",
    "runLayeredConnectionProbe(broker);",
])
if text("mqtt_cpp").count("conn.mqtt->publish") != 1:
    errors.append("Bambu MQTT may publish only the single read-only pushall request")
for command in ('"pause"', '"resume"', '"stop"', '"temperature"', '"ledctrl"'):
    if command in text("mqtt_cpp"):
        errors.append(f"Bambu MQTT contains forbidden control command: {command}")

require("portal_model_h", ["BambuPortalConfigInput", "accessToken", "printers", "activePrinterSerial", "mergeBambuPortalConfig"])
require("portal_cpp", ["WebServer server{8081}", '"/api/ha/status"', '"/api/ha/config"', '"/api/bambu/status"', '"/api/bambu/config"', '"/api/bambu/printers"', '"/api/bambu/discover"', '"/api/bambu/logout"', "Access Token", "打印机 ${i}", "当前打印机", "保存并切换", "用 Token 获取我的打印机", 'doc["token_set"]', 'doc["printer_count"]', 'doc["active_printer_serial"]', "extractBambuUserIdFromJwt", "discoverBambuPrinters", "sendSavedBambuPrinters"])
forbid("portal_cpp", ['"/api/bambu/login"', '"/api/bambu/verify"', '"/api/bambu/verification/resend"', "账号密码", "remember_password", "验证码", 'doc["password_set"]', 'doc["verification_required"]', 'doc["verification_type"]', 'doc["access_token"]', 'doc["token"]', "setInsecure"])

require("app_cpp", ["service_.configSnapshot()", "activeBambuPrinter", "InputEvent::PREV_SHORT", "InputEvent::NEXT_SHORT", "service_.cycleActivePrinter(-1)", "service_.cycleActivePrinter(1)", "presentationChanged", "if (changed) dirty_ = true"])
forbid("app_cpp", ["void BambuApp::onButton(InputEvent) {}"])
require("screen_h", ["struct RenderSignature", "signatureFor", "drawHeader", "drawProgress", "drawJob", "drawFilament", "drawFooter", "rendered_", "previous_"])
require("screen_cpp", ["BambuScreen::signatureFor", "const bool full = fullRedraw || !rendered_", "if (full) {", "display_->fillScreen(UiTheme::BACKGROUND);", "drawHeader", "drawProgress", "drawJob", "drawFilament", "drawFooter"])
if text("screen_cpp").count("display_->fillScreen(UiTheme::BACKGROUND);") != 1:
    errors.append("Bambu screen must clear the whole display only in the single full-redraw path")
forbid("screen_cpp", ["void BambuScreen::render(const BambuViewModel& model, bool) {"])

require("main", ["bambuMqttService.begin(bambuConfig, bambuConfigStore)", "integrationConfigPortal.begin(homeAssistantConfig, bambuCloudClient, bambuMqttService)", "WiFi.gatewayIP()", "WiFi.subnetMask()", "WiFi.dnsIP(0)", "[netcfg]"])
forbid("main", ["bambuMqttService.process(nowMs);"])
require("build", ['BAMBU_CONFIG_NAMESPACE[] = "bambucloud"', "BAMBU_MQTT_BUFFER_BYTES = 40960", "BAMBU_MQTT_CONNECT_TIMEOUT_SEC = 5U"])
require("pio", ["platform = espressif32@6.12.0", "knolleary/PubSubClient", "+<network/BambuPortalModel.cpp>"])
forbid("pio", ["MQTT_SOCKET_TIMEOUT"])
require("notices", ["Keralots/BambuHelper", "knolleary/PubSubClient"])

for path in sorted((ROOT / "src/network").glob("Bambu*.cpp")):
    if "setInsecure" in path.read_text(encoding="utf-8"):
        errors.append(f"{path.relative_to(ROOT)} contains forbidden setInsecure()")

if errors:
    for error in errors:
        print(f"ERROR: {error}")
    sys.exit(1)
print("Bambu manual-token + persistent multi-printer + background-worker + bounded read-only MQTT contract: OK")
