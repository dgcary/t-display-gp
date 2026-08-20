#!/usr/bin/env python3
"""Validate the Nixie Clock app integration contract."""
from pathlib import Path
import sys

required_files = {
    "src/app/NixieClockApp.h",
    "src/app/NixieClockApp.cpp",
    "src/app/NixieClockModel.h",
    "src/ui/NixieClockScreen.h",
    "src/ui/NixieClockScreen.cpp",
}
missing_files = sorted(path for path in required_files if not Path(path).exists())

checks = {
    "src/app/AppShell.h": ["NIXIE_CLOCK"],
    "src/main.cpp": [
        "NixieClockApp",
        "AppId::NIXIE_CLOCK",
        '"辉光时钟"',
    ],
    "src/app/NixieClockModel.h": [
        "struct NixieClockViewModel",
        "bool timeValid",
        "int hour",
        "int minute",
        "int second",
    ],
    "src/app/NixieClockApp.cpp": [
        "device_.localDateTime()",
        "1000U",
    ],
    "src/ui/NixieClockScreen.cpp": [
        "320",
        "170",
        "render",
    ],
}

missing_contract = []
for path, needles in checks.items():
    file_path = Path(path)
    if not file_path.exists():
        continue
    text = file_path.read_text(encoding="utf-8")
    for needle in needles:
        if needle not in text:
            missing_contract.append(f"{path}: {needle}")

forbidden_network_tokens = [
    "HTTPClient",
    "WiFiClientSecure",
    "HttpTransport",
    "NetworkArbiter",
    "AppDataWorker",
]
network_violations = []
for path in ("src/app/NixieClockApp.h", "src/app/NixieClockApp.cpp"):
    file_path = Path(path)
    if not file_path.exists():
        continue
    text = file_path.read_text(encoding="utf-8")
    for token in forbidden_network_tokens:
        if token in text:
            network_violations.append(f"{path}: {token}")

if missing_files or missing_contract or network_violations:
    if missing_files:
        print("missing Nixie Clock files:")
        for item in missing_files:
            print(f"  {item}")
    if missing_contract:
        print("missing Nixie Clock integration contract:")
        for item in missing_contract:
            print(f"  {item}")
    if network_violations:
        print("Nixie Clock must remain local-only; forbidden network dependency:")
        for item in network_violations:
            print(f"  {item}")
    sys.exit(1)

print("Nixie Clock local-only app + menu integration contract: OK")
