#!/usr/bin/env python3
"""Validate T-Display-S3 target, native/UI wiring and firmware artifact contract."""
from pathlib import Path
import sys

platformio = Path("platformio.ini").read_text(encoding="utf-8")
required_flags = {
    "default_envs = lilygo-t-display-s3", "build_unflags =", "-std=gnu++11", "-std=gnu++17",
    "-DUSER_SETUP_LOADED=1", "-DST7789_DRIVER=1", "-DINIT_SEQUENCE_3=1",
    "-DTFT_WIDTH=170", "-DTFT_HEIGHT=320", "-DCGRAM_OFFSET=1", "-DTFT_RGB_ORDER=TFT_RGB",
    "-DTFT_INVERSION_ON=1", "-DTFT_PARALLEL_8_BIT=1", "-DTFT_CS=6", "-DTFT_DC=7",
    "-DTFT_RST=5", "-DTFT_WR=8", "-DTFT_RD=9", "-DTFT_D0=39", "-DTFT_D1=40",
    "-DTFT_D2=41", "-DTFT_D3=42", "-DTFT_D4=45", "-DTFT_D5=46", "-DTFT_D6=47",
    "-DTFT_D7=48", "-DTFT_BL=38", "-DTFT_BACKLIGHT_ON=HIGH", "-DLOAD_GLCD=1",
    "-DLOAD_FONT2=1", "-DLOAD_FONT4=1", "-DSMOOTH_FONT=1", "-Isrc/generated",
}
required_native = {
    "test_build_src = true", "-Isrc/app", "-Isrc/network", "-Isrc/device", "-Isrc/ui",
    "+<app/AppShell.cpp>", "+<app/StockController.cpp>", "+<app/WeatherController.cpp>",
    "+<network/EastMoneyProvider.cpp>", "+<network/OpenMeteoProvider.cpp>",
    "+<network/TencentProvider.cpp>", "+<network/ProvisioningForm.cpp>",
    "+<ui/StockScreen.cpp>", "+<ui/BadApplePlayback.cpp>", "+<ui/WeatherVisuals.cpp>",
}
missing = sorted(x for x in required_flags if x not in platformio)
missing_native = sorted(x for x in required_native if x not in platformio)
present_forbidden = [x for x in ("-DTFT_RGB_ORDER=TFT_BGR", "+<ui/WeatherCatArt.cpp>") if x in platformio]

device = Path("src/device/DeviceLayer.cpp").read_text(encoding="utf-8")
stock_h = Path("src/ui/StockScreen.h").read_text(encoding="utf-8")
stock_cpp = Path("src/ui/StockScreen.cpp").read_text(encoding="utf-8")
weather_cpp = Path("src/ui/WeatherScreen.cpp").read_text(encoding="utf-8")
badapple_h = Path("src/ui/BadApplePlayback.h").read_text(encoding="utf-8")
workflow = Path(".github/workflows/ci.yml").read_text(encoding="utf-8")

missing_landscape = []
if "tft_.setRotation(3)" not in device:
    missing_landscape.append("DeviceLayer.cpp: tft_.setRotation(3)")
for token in ("SCREEN_WIDTH = 320", "SCREEN_HEIGHT = 170"):
    if token not in stock_h:
        missing_landscape.append(f"StockScreen.h: {token}")

required_ui = {
    "StockScreen.cpp": [
        "StockScreenText::providerSummary(model.provider, model.intradayProvider, model.hasIntraday)",
        "previous_.intradayProvider != next.intradayProvider",
    ],
    "WeatherScreen.cpp": [
        "renderAnimation", "drawBadApple", "BAD_APPLE_X = 152", "BAD_APPLE_W = 168",
        "drawForecastRow(*unicodeFont_, 138, \"今\"", "drawForecastRow(*unicodeFont_, 152, \"明\"",
    ],
    "BadApplePlayback.h": ["WIDTH = 168", "HEIGHT = 126", "FPS = 10", "FRAME_COUNT = 2190U"],
}
missing_ui = []
for token in required_ui["StockScreen.cpp"]:
    if token not in stock_cpp:
        missing_ui.append(f"StockScreen.cpp: {token}")
for token in required_ui["WeatherScreen.cpp"]:
    if token not in weather_cpp:
        missing_ui.append(f"WeatherScreen.cpp: {token}")
for token in required_ui["BadApplePlayback.h"]:
    if token not in badapple_h:
        missing_ui.append(f"BadApplePlayback.h: {token}")
for token in ("WeatherCatArt", "drawWatercolorCat", "dayAfter"):
    if token in weather_cpp:
        missing_ui.append(f"WeatherScreen.cpp forbidden: {token}")

required_artifact = {
    "actions/upload-artifact@v4", "tdisplay-gp-firmware-", ".pio/build/lilygo-t-display-s3/firmware.bin",
    ".pio/build/lilygo-t-display-s3/partitions.bin", ".pio/build/lilygo-t-display-s3/bootloader.bin",
    "firmware-manifest.txt", "python tools/prepare_bad_apple_asset.py",
}
missing_artifact = sorted(x for x in required_artifact if x not in workflow)

if missing or missing_native or present_forbidden or missing_landscape or missing_ui or missing_artifact:
    if missing:
        print("missing TFT/C++ build contract flags:")
        for x in missing: print(f"  {x}")
    if missing_native:
        print("missing native-test wiring:")
        for x in missing_native: print(f"  {x}")
    if present_forbidden:
        print("forbidden build wiring:")
        for x in present_forbidden: print(f"  {x}")
    if missing_landscape:
        print("missing landscape contract:")
        for x in missing_landscape: print(f"  {x}")
    if missing_ui:
        print("missing UI wiring contract:")
        for x in missing_ui: print(f"  {x}")
    if missing_artifact:
        print("missing prebuilt artifact workflow contract:")
        for x in missing_artifact: print(f"  {x}")
    sys.exit(1)

print("T-Display-S3 TFT + C++17 + 320x170 + Bad Apple Weather + prebuilt firmware artifact contract: OK")
