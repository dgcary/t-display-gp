#!/usr/bin/env python3
"""Validate direct button-driven page navigation without a menu screen."""
from pathlib import Path
import sys

shell_h = Path("src/app/AppShell.h").read_text(encoding="utf-8")
shell_cpp = Path("src/app/AppShell.cpp").read_text(encoding="utf-8")
stock_h = Path("src/app/StockApp.h").read_text(encoding="utf-8")
bambu_h = Path("src/app/BambuApp.h").read_text(encoding="utf-8")
main = Path("src/main.cpp").read_text(encoding="utf-8")

missing = []
for needle in [
    "virtual size_t pageCount() const",
    "virtual bool selectPage(size_t pageIndex)",
    "AppManager(std::initializer_list<IApp*> apps);",
    "bool begin();",
    "bool navigate(int direction);",
]:
    if needle not in shell_h:
        missing.append(f"src/app/AppShell.h: {needle}")

for needle in [
    "InputEvent::PREV_SHORT",
    "InputEvent::NEXT_SHORT",
    "navigate(-1)",
    "navigate(1)",
]:
    if needle not in shell_cpp:
        missing.append(f"src/app/AppShell.cpp: {needle}")

for needle in [
    "MAX_NAVIGATION_PAGES = 4U",
    "size_t pageCount() const override",
    "bool selectPage(size_t pageIndex) override",
]:
    if needle not in stock_h:
        missing.append(f"src/app/StockApp.h: {needle}")

for needle in [
    "MAX_NAVIGATION_PAGES = 2U",
    "size_t pageCount() const override",
    "bool selectPage(size_t pageIndex) override",
]:
    if needle not in bambu_h:
        missing.append(f"src/app/BambuApp.h: {needle}")

for needle in [
    "AppManager appManager({&weatherApp, &stockApp, &bambuApp, &homeAssistantApp, &deviceInfoApp});",
    "appManager.begin()",
]:
    if needle not in main:
        missing.append(f"src/main.cpp: {needle}")

forbidden = []
for needle in [
    "MenuApp menuApp",
    "MenuScreen menuScreen",
    '#include "MenuScreen.h"',
    "appManager.begin(AppId::STOCK)",
]:
    if needle in main:
        forbidden.append(f"src/main.cpp: {needle}")

if "switchTo(AppId::MENU)" in shell_cpp:
    forbidden.append("src/app/AppShell.cpp: menu return path")

if missing or forbidden:
    if missing:
        print("missing direct-navigation contract:")
        for item in missing:
            print(f"  {item}")
    if forbidden:
        print("forbidden menu-navigation remnants:")
        for item in forbidden:
            print(f"  {item}")
    sys.exit(1)

print("Direct weather -> stock[1..4] -> Bambu[1..2] -> HA -> device navigation contract: OK")
