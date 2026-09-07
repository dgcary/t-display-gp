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


require("config_h", ["struct BambuPrinterConfig", "PRINTER_COUNT = 4", "printers", "printerCount", "activePrinterIndex", "activeBambuPrinter"])
forbid("config_h", ["std::string email;", "std::string password;", "verificationCode", "tfaKey"])
require("config_cpp", ["CONFIG_SCHEMA_V2 = 2", "decodeV1", 'createNestedArray("printers")', 'doc["active_printer"]'])
forbid("config_cpp", ['doc["email"]', 'doc["password"]'])
require("store_cpp", ["legacySchema", "save(parsed)"])
forbid("store_cpp", ["Serial"])

require("client_h", ["class BambuCloudClient", "fetchUserId(", "fetchPrinters("])
forbid("client_h", ["BambuCloudLoginResult", "BambuVerificationType", "login(", "submitVerificationCode(", "requestVerificationCode(", "submitTfaCode("])
require("client_cpp", ["WiFiClientSecure", "HTTPClient", "setCACertBundle", "rootca_crt_bundle_start", "sharedNetworkArbiter", '"/v1/user-service/my/profile"', '"/api/v1/iot-service/api/user/bind"', "extractBambuUserIdFromJwt"])
forbid("client_cpp", ["setInsecure", '"/v1/user-service/user/login"', "sendsmscode", "sendemail/code", '"/api/sign-in/tfa"', '"/api/csrf"', "Serial"])

require("mqtt_h", ["class BambuMqttService", "snapshot()", "status()", "configSnapshot()", "replaceConfig(", "externalConfigRevision_", "tokenRejected_"])
forbid("mqtt_h", ["BambuCloudClient", "BambuSessionModel sessionModel_", "setPendingVerification", "submitVerificationCode", "requestVerificationCode", "pendingTfaKey", "passwordSet", "verificationRequired", "verificationType"])
require("mqtt_cpp", ["PubSubClient", "WiFiClientSecure", "setCACertBundle", "rootca_crt_bundle_start", "setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)", "sharedNetworkArbiter", "activeBambuPrinter", "bambuBrokerForRegion", "bambuReportTopic", "pushall", "mqtt_->loop()", "externalConfigRevision_", "state_ = BambuState{}", "tokenRejected_"])
forbid("mqtt_cpp", ["setInsecure", "performRelogin", "setPendingVerification", "submitVerificationCode", "requestVerificationCode", "fetchPrinters(", "fetchUserId(", "VERIFICATION_REQUIRED", "result=CHALLENGE"])
if text("mqtt_cpp").count("mqtt_->publish") != 1:
    errors.append("Bambu MQTT may publish only the single read-only pushall request")
for command in ('"pause"', '"resume"', '"stop"', '"temperature"'):
    if command in text("mqtt_cpp"):
        errors.append(f"Bambu MQTT contains forbidden control command: {command}")

require("portal_model_h", ["BambuPortalConfigInput", "accessToken", "printers", "activePrinterSerial", "mergeBambuPortalConfig"])
require("portal_cpp", ["WebServer server{8081}", '"/api/ha/status"', '"/api/ha/config"', '"/api/bambu/status"', '"/api/bambu/config"', '"/api/bambu/printers"', '"/api/bambu/discover"', '"/api/bambu/logout"', "Access Token", "打印机 ${i}", "当前打印机", "保存并切换", "用 Token 获取我的打印机", 'doc["token_set"]', 'doc["printer_count"]', 'doc["active_printer_serial"]', "extractBambuUserIdFromJwt", "discoverBambuPrinters", "sendSavedBambuPrinters"])
forbid("portal_cpp", ['"/api/bambu/login"', '"/api/bambu/verify"', '"/api/bambu/verification/resend"', "账号密码", "remember_password", "验证码", 'doc["password_set"]', 'doc["verification_required"]', 'doc["verification_type"]', 'doc["access_token"]', 'doc["token"]', "setInsecure"])

require("app_cpp", ["service_.configSnapshot()", "activeBambuPrinter"])
forbid("app_cpp", ["config.printerName"])
require("main", ["bambuMqttService.begin(bambuConfig, bambuConfigStore)", "integrationConfigPortal.begin(homeAssistantConfig, bambuCloudClient, bambuMqttService)"])
require("build", ['BAMBU_CONFIG_NAMESPACE[] = "bambucloud"', "BAMBU_MQTT_BUFFER_BYTES = 40960"])
require("pio", ["knolleary/PubSubClient", "+<network/BambuPortalModel.cpp>"])
require("notices", ["Keralots/BambuHelper", "knolleary/PubSubClient"])

for path in sorted((ROOT / "src/network").glob("Bambu*.cpp")):
    if "setInsecure" in path.read_text(encoding="utf-8"):
        errors.append(f"{path.relative_to(ROOT)} contains forbidden setInsecure()")

if errors:
    for error in errors:
        print(f"ERROR: {error}")
    sys.exit(1)
print("Bambu manual-token + local multi-printer + read-only persistent MQTT contract: OK")
