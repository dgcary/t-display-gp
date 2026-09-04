#!/usr/bin/env python3
"""Validate the current app-shell cleanup contract."""
from pathlib import Path
import sys

shell = Path("src/app/AppShell.h").read_text(encoding="utf-8")
main = Path("src/main.cpp").read_text(encoding="utf-8")

missing = []
for needle in [
    "bool begin(AppId startupApp = AppId::STOCK);",
]:
    if needle not in shell:
        missing.append(f"src/app/AppShell.h: {needle}")

for needle in [
    "AppId::STOCK",
    '"股票"',
    '"天气"',
    '"智能家居"',
    '"设备信息"',
    "appManager.begin(AppId::STOCK)",
]:
    if needle not in main:
        missing.append(f"src/main.cpp: {needle}")

forbidden = []
for path, text in [("src/app/AppShell.h", shell), ("src/main.cpp", main)]:
    for needle in ["NIXIE_CLOCK", "CRYPTO", "NixieClockApp", "CryptoApp"]:
        if needle in text:
            forbidden.append(f"{path}: {needle}")

for path, text in [("src/app/AppShell.h", shell), ("src/main.cpp", main)]:
    for needle in ["IDLE_TO_NIXIE", "idleDeadline", "switchTo(AppId::NIXIE_CLOCK)"]:
        if needle in text:
            forbidden.append(f"{path}: idle switch {needle}")

if missing or forbidden:
    if missing:
        print("missing app-shell contract:")
        for item in missing:
            print(f"  {item}")
    if forbidden:
        print("forbidden app-shell remnants:")
        for item in forbidden:
            print(f"  {item}")
    sys.exit(1)

print("Stock startup + four-app cleanup shell contract: OK")
