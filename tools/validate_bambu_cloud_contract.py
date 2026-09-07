from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
CONFIG_H = ROOT / "src/network/BambuConfig.h"
CONFIG_CPP = ROOT / "src/network/BambuConfig.cpp"
CLIENT_H = ROOT / "src/network/BambuCloudClient.h"
CLIENT_CPP = ROOT / "src/network/BambuCloudClient.cpp"
MQTT_H = ROOT / "src/network/BambuMqttService.h"
MQTT_CPP = ROOT / "src/network/BambuMqttService.cpp"
PORTAL_CPP = ROOT / "src/network/IntegrationConfigPortal.cpp"
PORTAL_MODEL_H = ROOT / "src/network/BambuPortalModel.h"
APP_CPP = ROOT / "src/app/BambuApp.cpp"
MAIN = ROOT / "src/main.cpp"
BUILD_CONFIG = ROOT / "include/build_config.h"
PLATFORMIO = ROOT / "platformio.ini"
NOTICES = ROOT / "THIRD_PARTY_NOTICES.md"

errors = []
files = [CONFIG_H, CONFIG_CPP, CLIENT_H, CLIENT_CPP, MQTT_H, MQTT_CPP,
         PORTAL_CPP, PORTAL_MODEL_H, APP_CPP, MAIN, BUILD_CONFIG, PLATFORMIO, NOTICES]
for path in files:
    if not path.exists():
        errors.append(f"missing {path.relative_to(ROOT)}")


def require(text, markers, where):
    for marker in markers:
        if marker not in text:
            errors.append(f"{where} missing marker: {marker}")


def forbid(text, markers, where):
    for marker in markers:
        if marker in text:
            errors.append(f"{where} contains obsolete/forbidden marker: {marker}")


if CONFIG_H.exists() and CONFIG_CPP.exists():
    h = CONFIG_H.read_text(encoding="utf-8")
    c = CONFIG_CPP.read_text(encoding="utf-8")
    require(h, ["struct BambuPrinterConfig", "PRINTER_COUNT = 4", "printers", "printerCount",
                "activePrinterIndex", "activeBambuPrinter"], "BambuConfig.h")
    require(c, ["CONFIG_SCHEMA_V2 = 2", "decodeV1", 'doc["printers"]',
                'doc["active_printer"]'], "BambuConfig.cpp")
    forbid(h, ["std::string email;", "std::string password;", "verificationCode", "tfaKey"],
           "BambuConfig.h")
    forbid(c, ['doc["email"]', 'doc["password"]'], "BambuConfig.cpp")

if CLIENT_H.exists() and CLIENT_CPP.exists():
    h = CLIENT_H.read_text(encoding="utf-8")
    c = CLIENT_CPP.read_text(encoding="utf-8")
    require(h, ["class BambuCloudClient", "fetchUserId(", "fetchPrinters("], "BambuCloudClient.h")
    forbid(h, ["BambuCloudLoginResult", "BambuVerificationType", "login(",
               "submitVerificationCode(", "requestVerificationCode(", "submitTfaCode("],
           "BambuCloudClient.h")
    require(c, ["WiFiClientSecure", "HTTPClient", "setCACertBundle", "rootca_crt_bundle_start",
                "sharedNetworkArbiter", '"/v1/user-service/my/profile"',
                '"/api/v1/iot-service/api/user/bind"', "extractBambuUserIdFromJwt"],
            "BambuCloudClient.cpp")
    forbid(c, ["setInsecure", '"/v1/user-service/user/login"', "sendsmscode",
               "sendemail/code", '"/api/sign-in/tfa"', '"/api/csrf"', "Serial"],
           "BambuCloudClient.cpp")

if MQTT_H.exists() and MQTT_CPP.exists():
    h = MQTT_H.read_text(encoding="utf-8")
    c = MQTT_CPP.read_text(encoding="utf-8")
    require(h, ["class BambuMqttService", "snapshot()", "status()", "configSnapshot()",
                "replaceConfig(", "externalConfigRevision_"], "BambuMqttService.h")
    forbid(h, ["BambuCloudClient", "BambuSessionModel", "setPendingVerification",
               "submitVerificationCode", "requestVerificationCode", "pendingTfaKey",
               "passwordSet", "verificationRequired", "verificationType"], "BambuMqttService.h")
    require(c, ["PubSubClient", "WiFiClientSecure", "setCACertBundle", "rootca_crt_bundle_start",
                "setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)", "sharedNetworkArbiter",
                "activeBambuPrinter", "bambuBrokerForRegion", "bambuReportTopic", "pushall",
                "mqtt_->loop()", "externalConfigRevision_", "state_ = BambuState{}"],
            "BambuMqttService.cpp")
    forbid(c, ["setInsecure", "performRelogin", "setPendingVerification", "submitVerificationCode",
               "requestVerificationCode", "fetchPrinters(", "fetchUserId(", "VERIFICATION_REQUIRED",
               "result=CHALLENGE"], "BambuMqttService.cpp")
    if c.count("mqtt_->publish") != 1:
        errors.append("Bambu MQTT may publish only the single read-only pushall request")
    for control in ('"pause"', '"resume"', '"stop"', '"temperature"'):
        if control in c:
            errors.append(f"Bambu MQTT contains forbidden control command: {control}")

if PORTAL_CPP.exists() and PORTAL_MODEL_H.exists():
    p = PORTAL_CPP.read_text(encoding="utf-8")
    m = PORTAL_MODEL_H.read_text(encoding="utf-8")
    require(m, ["BambuPortalConfigInput", "accessToken", "printers", "activePrinterSerial",
                "mergeBambuPortalConfig"], "BambuPortalModel.h")
    require(p, ["WebServer server{8081}", '"/api/ha/status"', '"/api/ha/config"',
                '"/api/bambu/status"', '"/api/bambu/config"', '"/api/bambu/printers"',
                '"/api/bambu/discover"', '"/api/bambu/logout"', "Access Token", "打印机 1",
                "当前打印机", "保存并切换", "用 Token 获取我的打印机",
                'doc["token_set"]', 'doc["printer_count"]', 'doc["active_printer_serial"]',
                "extractBambuUserIdFromJwt", "discoverBambuPrinters", "sendSavedBambuPrinters"],
            "IntegrationConfigPortal.cpp")
    forbid(p, ['"/api/bambu/login"', '"/api/bambu/verify"', '"/api/bambu/verification/resend"',
               "账号密码", "remember_password", "验证码", 'doc["password_set"]',
               'doc["verification_required"]', 'doc["verification_type"]',
               'doc["access_token"]', 'doc["token"]', "setInsecure"],
           "IntegrationConfigPortal.cpp")

if APP_CPP.exists():
    a = APP_CPP.read_text(encoding="utf-8")
    require(a, ["service_.configSnapshot()", "activeBambuPrinter"], "BambuApp.cpp")
    forbid(a, ["config.printerName"], "BambuApp.cpp")

if MAIN.exists():
    main = MAIN.read_text(encoding="utf-8")
    require(main, ["bambuMqttService.begin(bambuConfig, bambuConfigStore)",
                   "integrationConfigPortal.begin(homeAssistantConfig, bambuCloudClient, bambuMqttService)"],
            "main.cpp")

if BUILD_CONFIG.exists():
    b = BUILD_CONFIG.read_text(encoding="utf-8")
    require(b, ['BAMBU_CONFIG_NAMESPACE[] = "bambucloud"', "BAMBU_MQTT_BUFFER_BYTES = 40960"],
            "build_config.h")

if PLATFORMIO.exists():
    pio = PLATFORMIO.read_text(encoding="utf-8")
    require(pio, ["knolleary/PubSubClient", "+<network/BambuPortalModel.cpp>"], "platformio.ini")

if NOTICES.exists():
    n = NOTICES.read_text(encoding="utf-8")
    require(n, ["Keralots/BambuHelper", "knolleary/PubSubClient"], "THIRD_PARTY_NOTICES.md")

for path in sorted((ROOT / "src/network").glob("Bambu*.cpp")):
    text = path.read_text(encoding="utf-8")
    if "setInsecure" in text:
        errors.append(f"{path.relative_to(ROOT)} contains forbidden setInsecure()")

if errors:
    for error in errors:
        print(f"ERROR: {error}")
    sys.exit(1)

print("Bambu manual-token + local multi-printer + read-only persistent MQTT contract: OK")
