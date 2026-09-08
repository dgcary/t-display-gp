#!/usr/bin/env python3
"""Validate the final five-app shell contract."""
from pathlib import Path
import sys

shell = Path("src/app/AppShell.h").read_text(encoding="utf-8")
main = Path("src/main.cpp").read_text(encoding="utf-8")

missing = []
for needle in [
    "BAMBU",
    "bool begin(AppId startupApp = AppId::STOCK);",
]:
    if needle not in shell:
        missing.append(f"src/app/AppShell.h: {needle}")

for needle in [
    "AppId::STOCK",
    "AppId::WEATHER",
    "AppId::BAMBU",
    "AppId::HOME_ASSISTANT",
    "AppId::DEVICE_INFO",
    '"股票"',
    '"天气"',
    '"Bambu Lab"',
    '"智能家居"',
    '"设备信息"',
    '#include "BambuApp.h"',
    "appManager.begin(AppId::STOCK)",
    'case AppId::BAMBU: return "BAMBU";',
]:
    if needle not in main:
        missing.append(f"src/main.cpp: {needle}")

for path in ["src/app/BambuApp.h", "src/app/BambuApp.cpp", "src/ui/BambuScreen.h", "src/ui/BambuScreen.cpp"]:
    if not Path(path).exists():
        missing.append(f"missing {path}")

forbidden = []
for path, text in [("src/app/AppShell.h", shell), ("src/main.cpp", main)]:
    for needle in ["NIXIE_CLOCK", "CRYPTO", "NixieClockApp", "CryptoApp"]:
        if needle in text:
            forbidden.append(f"{path}: {needle}")
    for needle in ["IDLE_TO_NIXIE", "idleDeadline", "switchTo(AppId::NIXIE_CLOCK)"]:
        if needle in text:
            forbidden.append(f"{path}: idle switch {needle}")

# The final main wiring must keep menu order stable.
ordered = ['"股票"', '"天气"', '"Bambu Lab"', '"智能家居"', '"设备信息"']
positions = [main.find(item) for item in ordered]
if all(pos >= 0 for pos in positions) and positions != sorted(positions):
    missing.append("src/main.cpp: final menu order Stock/Weather/Bambu/HA/DeviceInfo")

if missing or forbidden:
    if missing:
        print("missing final app-shell contract:")
        for item in missing:
            print(f"  {item}")
    if forbidden:
        print("forbidden app-shell remnants:")
        for item in forbidden:
            print(f"  {item}")
    sys.exit(1)

print("Stock startup + final five-app Bambu shell contract: OK")
