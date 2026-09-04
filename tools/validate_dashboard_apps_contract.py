#!/usr/bin/env python3
"""Validate Home Assistant dashboard integration and security contracts."""
from pathlib import Path
import sys

required_files = {
    "src/app/HomeAssistantApp.h", "src/app/HomeAssistantApp.cpp",
    "src/app/HomeAssistantController.h", "src/app/HomeAssistantController.cpp",
    "src/ui/HomeAssistantScreen.h", "src/ui/HomeAssistantScreen.cpp",
    "src/network/HomeAssistantProvider.h", "src/network/HomeAssistantProvider.cpp",
    "src/network/SecureHomeAssistantTransport.cpp",
    "src/network/HomeAssistantConfig.h", "src/network/HomeAssistantConfig.cpp",
    "src/network/HomeAssistantConfigStore.h", "src/network/HomeAssistantConfigStore.cpp",
    "src/network/IntegrationConfigPortal.h", "src/network/IntegrationConfigPortal.cpp",
}
checks = {
    "src/app/AppShell.h": ["HOME_ASSISTANT"],
    "src/main.cpp": ["HomeAssistantApp", '"智能家居"', "AppId::HOME_ASSISTANT", "AppDataWorker appDataWorker", "IntegrationConfigPortal"],
    "src/network/AppDataTypes.h": ["HOME_ASSISTANT", "tryReceive(AppDataRequestType type"],
    "src/network/AppDataWorker.cpp": ["HomeAssistantProvider", "AppDataRequestType::HOME_ASSISTANT", "OpenMeteoProvider"],
    "src/network/HomeAssistantConfig.cpp": ["http://", "https://", "isHttpsUrl", "CA_CERT"],
    "src/network/SecureHomeAssistantTransport.cpp": ["Authorization", "Bearer ", "WiFiClient client", "setCACert", "/api/states/", "NetworkArbiter", '"HA_HTTP"', '"HA_CA"'],
    "src/network/IntegrationConfigPortal.cpp": ["WebServer server{8081}", "ha_token_set", "ha_ca_set", "http://homeassistant.local:8123", '"/api/ha/status"', '"/api/ha/config"'],
}
missing_files = sorted(path for path in required_files if not Path(path).exists())
missing_contract = []
for path, needles in checks.items():
    file_path = Path(path)
    if not file_path.exists():
        continue
    text = file_path.read_text(encoding="utf-8")
    for needle in needles:
        if needle not in text:
            missing_contract.append(f"{path}: {needle}")

security_violations = []
ha_transport = Path("src/network/SecureHomeAssistantTransport.cpp")
if ha_transport.exists() and "setInsecure" in ha_transport.read_text(encoding="utf-8"):
    security_violations.append("Home Assistant HTTPS transport must never use setInsecure")
portal = Path("src/network/IntegrationConfigPortal.cpp")
if portal.exists():
    text = portal.read_text(encoding="utf-8")
    status_start = text.find("void sendHaStatus")
    status_end = text.find("void saveHa")
    status_region = text[status_start:status_end] if status_start >= 0 and status_end > status_start else ""
    if 'doc["token"]' in status_region or 'doc["ca_cert"]' in status_region:
        security_violations.append("HA status endpoint must not expose token or CA contents")
main_cpp = Path("src/main.cpp")
if main_cpp.exists() and main_cpp.read_text(encoding="utf-8").count("AppDataWorker appDataWorker") != 1:
    security_violations.append("exactly one shared AppDataWorker is required")
worker = Path("src/network/AppDataWorker.cpp")
if worker.exists() and "Crypto" in worker.read_text(encoding="utf-8"):
    security_violations.append("shared AppDataWorker must no longer contain Crypto paths")
if Path("src/network/HomeAssistantConfigPortal.cpp").exists() or Path("src/network/HomeAssistantConfigPortal.h").exists():
    security_violations.append("HA must share the single IntegrationConfigPortal on port 8081")

if missing_files or missing_contract or security_violations:
    if missing_files:
        print("missing Home Assistant files:")
        for item in missing_files:
            print(f"  {item}")
    if missing_contract:
        print("missing Home Assistant integration contract:")
        for item in missing_contract:
            print(f"  {item}")
    if security_violations:
        print("Home Assistant security/architecture violations:")
        for item in security_violations:
            print(f"  {item}")
    sys.exit(1)

print("Home Assistant HTTP + verified HTTPS shared-worker + unified portal contract: OK")
