#!/usr/bin/env python3
"""Validate Home Assistant + Crypto dashboard integration and security contracts."""
from pathlib import Path
import sys

required_files = {
    "src/app/HomeAssistantApp.h",
    "src/app/HomeAssistantApp.cpp",
    "src/app/HomeAssistantController.h",
    "src/app/HomeAssistantController.cpp",
    "src/ui/HomeAssistantScreen.h",
    "src/ui/HomeAssistantScreen.cpp",
    "src/network/HomeAssistantProvider.h",
    "src/network/HomeAssistantProvider.cpp",
    "src/app/CryptoApp.h",
    "src/app/CryptoApp.cpp",
    "src/app/CryptoController.h",
    "src/app/CryptoController.cpp",
    "src/ui/CryptoScreen.h",
    "src/ui/CryptoScreen.cpp",
    "src/network/CryptoProvider.h",
    "src/network/CryptoProvider.cpp",
}

checks = {
    "src/app/AppShell.h": ["HOME_ASSISTANT", "CRYPTO"],
    "src/main.cpp": [
        "HomeAssistantApp",
        "CryptoApp",
        '"智能家居"',
        '"加密货币"',
        "AppId::HOME_ASSISTANT",
        "AppId::CRYPTO",
    ],
    "src/network/AppDataTypes.h": [
        "HOME_ASSISTANT",
        "CRYPTO",
        "tryReceive(AppDataRequestType type",
    ],
    "src/network/AppDataWorker.cpp": [
        "HomeAssistantProvider",
        "CryptoProvider",
        "AppDataRequestType::HOME_ASSISTANT",
        "AppDataRequestType::CRYPTO",
    ],
    "src/network/CryptoProvider.cpp": [
        "api.coingecko.com/api/v3/simple/price",
        "bitcoin,ethereum,solana",
        "include_24hr_change=true",
    ],
    "src/network/HomeAssistantProvider.cpp": [
        "Authorization",
        "Bearer ",
        "setCACert",
        "/api/states/",
    ],
    "src/network/ProvisioningService.cpp": [
        "ha_token_set",
        "ha_ca_set",
    ],
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
ha_provider = Path("src/network/HomeAssistantProvider.cpp")
if ha_provider.exists():
    text = ha_provider.read_text(encoding="utf-8")
    if "setInsecure" in text:
        security_violations.append("HomeAssistantProvider must not use setInsecure")

main_cpp = Path("src/main.cpp")
if main_cpp.exists():
    text = main_cpp.read_text(encoding="utf-8")
    if text.count("AppDataWorker appDataWorker") != 1:
        security_violations.append("exactly one shared AppDataWorker is required")

if missing_files or missing_contract or security_violations:
    if missing_files:
        print("missing dashboard app files:")
        for item in missing_files:
            print(f"  {item}")
    if missing_contract:
        print("missing dashboard integration contract:")
        for item in missing_contract:
            print(f"  {item}")
    if security_violations:
        print("dashboard security/architecture violations:")
        for item in security_violations:
            print(f"  {item}")
    sys.exit(1)

print("Home Assistant strict-TLS + Crypto shared-worker dashboard contract: OK")
