#!/usr/bin/env python3
"""Validate the approved large Bad Apple Weather layout/player contract."""
from pathlib import Path
import sys

required_files = {
    "src/ui/BadApplePlayback.h",
    "src/ui/BadApplePlayback.cpp",
    "tools/prepare_bad_apple_asset.py",
}

checks = {
    "src/ui/WeatherScreen.cpp": [
        "BAD_APPLE_X = 152",
        "BAD_APPLE_Y = 27",
        "BAD_APPLE_W = 168",
        "BAD_APPLE_H = 126",
        "drawBadApple",
    ],
    "src/app/WeatherApp.cpp": [
        "BadApplePlayback::frameIndex",
    ],
    "tools/prepare_bad_apple_asset.py": [
        "168",
        "126",
        "10",
        "ea6954fcca5172ab8b32c6bfc68f8d042c1d59a8",
        "fb8ba0b0969a508e73d2421c6571c12e0fe7b103",
    ],
}

missing = [p for p in sorted(required_files) if not Path(p).exists()]
contract = []
for path, needles in checks.items():
    p = Path(path)
    if not p.exists():
        continue
    text = p.read_text(encoding="utf-8")
    for needle in needles:
        if needle not in text:
            contract.append(f"{path}: {needle}")

forbidden = []
weather = Path("src/ui/WeatherScreen.cpp")
if weather.exists():
    text = weather.read_text(encoding="utf-8")
    for token in (
        "WeatherCatArt",
        "drawWatercolorCat",
        "dayAfter",
        "display_->drawFastHLine(0, 24, 320, UiTheme::GRID)",
        "display_->drawFastVLine(150, 27, 126, UiTheme::GRID)",
    ):
        if token in text:
            forbidden.append(f"src/ui/WeatherScreen.cpp: {token}")

app = Path("src/app/WeatherApp.cpp")
if app.exists():
    text = app.read_text(encoding="utf-8")
    for token in ("xTaskCreate", "NetworkArbiter", "HttpTransport"):
        if token in text:
            forbidden.append(f"src/app/WeatherApp.cpp: {token}")

if missing or contract or forbidden:
    if missing:
        print("missing Bad Apple files:")
        for item in missing:
            print(f"  {item}")
    if contract:
        print("missing Bad Apple layout/player contract:")
        for item in contract:
            print(f"  {item}")
    if forbidden:
        print("forbidden old/architectural Weather behavior:")
        for item in forbidden:
            print(f"  {item}")
    sys.exit(1)

print("Bad Apple Weather 168x126 + 10fps + two-day compact no-divider layout contract: OK")
