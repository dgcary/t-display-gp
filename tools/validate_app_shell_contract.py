#!/usr/bin/env python3
"""Validate the final direct-navigation app shell contract."""
from pathlib import Path
import sys

shell = Path("src/app/AppShell.h").read_text(encoding="utf-8")
main = Path("src/main.cpp").read_text(encoding="utf-8")

missing = []
for needle in [
    "BAMBU",
    "explicit AppManager(std::initializer_list<IApp*> apps);",
    "bool begin();",
    "size_t activePageIndex() const;",
]:
    if needle not in shell:
        missing.append(f"src/app/AppShell.h: {needle}")

for needle in [
    "AppId::STOCK",
    "AppId::WEATHER",
    "AppId::BAMBU",
    "AppId::HOME_ASSISTANT",
    "AppId::DEVICE_INFO",
    '#include "BambuApp.h"',
    "AppManager appManager({&weatherApp, &stockApp, &bambuApp, &homeAssistantApp, &deviceInfoApp});",
    "appManager.begin()",
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

for needle in ["MenuApp menuApp", "MenuScreen menuScreen", '#include "MenuScreen.h"', "appManager.begin(AppId::STOCK)"]:
    if needle in main:
        forbidden.append(f"src/main.cpp: {needle}")

if missing or forbidden:
    if missing:
        print("missing final direct app-shell contract:")
        for item in missing:
            print(f"  {item}")
    if forbidden:
        print("forbidden app-shell remnants:")
        for item in forbidden:
            print(f"  {item}")
    sys.exit(1)

print("Weather startup + direct paged navigation shell contract: OK")
