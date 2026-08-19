#!/usr/bin/env python3
"""Validate the T-Display-S3 TFT, orientation, and native-test wiring contract."""
from pathlib import Path
import sys

text = Path("platformio.ini").read_text(encoding="utf-8")
required = {
    "-DUSER_SETUP_LOADED=1",
    "-DST7789_DRIVER=1",
    "-DINIT_SEQUENCE_3=1",
    "-DTFT_WIDTH=170",
    "-DTFT_HEIGHT=320",
    "-DCGRAM_OFFSET=1",
    "-DTFT_RGB_ORDER=TFT_RGB",
    "-DTFT_INVERSION_ON=1",
    "-DTFT_PARALLEL_8_BIT=1",
    "-DTFT_CS=6",
    "-DTFT_DC=7",
    "-DTFT_RST=5",
    "-DTFT_WR=8",
    "-DTFT_RD=9",
    "-DTFT_D0=39",
    "-DTFT_D1=40",
    "-DTFT_D2=41",
    "-DTFT_D3=42",
    "-DTFT_D4=45",
    "-DTFT_D5=46",
    "-DTFT_D6=47",
    "-DTFT_D7=48",
    "-DTFT_BL=38",
    "-DTFT_BACKLIGHT_ON=HIGH",
}
missing = sorted(flag for flag in required if flag not in text)

required_native_test_wiring = {
    "test_build_src = true",
    "-Isrc/app",
    "-Isrc/network",
    "-Isrc/device",
    "-Isrc/ui",
    "+<app/AppShell.cpp>",
    "+<app/StockController.cpp>",
    "+<app/WeatherController.cpp>",
    "+<network/EastMoneyProvider.cpp>",
    "+<network/OpenMeteoProvider.cpp>",
    "+<network/TencentProvider.cpp>",
    "+<network/ProvisioningForm.cpp>",
    "+<ui/StockScreen.cpp>",
    "+<ui/WeatherVisuals.cpp>",
}
missing_native = sorted(item for item in required_native_test_wiring if item not in text)

forbidden = ["-DTFT_RGB_ORDER=TFT_BGR"]
present_forbidden = [flag for flag in forbidden if flag in text]

device = Path("src/device/DeviceLayer.cpp").read_text(encoding="utf-8")
screen = Path("src/ui/StockScreen.h").read_text(encoding="utf-8")
stock_screen_cpp = Path("src/ui/StockScreen.cpp").read_text(encoding="utf-8")
weather_screen_cpp = Path("src/ui/WeatherScreen.cpp").read_text(encoding="utf-8")
required_landscape = {
    "DeviceLayer.cpp": ["tft_.setRotation(3)"],
    "StockScreen.h": ["SCREEN_WIDTH = 320", "SCREEN_HEIGHT = 170"],
}
missing_landscape = []
for needle in required_landscape["DeviceLayer.cpp"]:
    if needle not in device:
        missing_landscape.append(f"DeviceLayer.cpp: {needle}")
for needle in required_landscape["StockScreen.h"]:
    if needle not in screen:
        missing_landscape.append(f"StockScreen.h: {needle}")

required_ui_wiring = {
    "StockScreen.cpp": [
        "StockScreenText::providerSummary(model.provider, model.intradayProvider, model.hasIntraday)",
        "previous_.intradayProvider != next.intradayProvider",
    ],
    "WeatherScreen.cpp": [
        "font.print(conditionName(day.weatherCode))",
        "renderAnimation",
        "WeatherVisuals::catMood",
    ],
}
missing_ui = []
for needle in required_ui_wiring["StockScreen.cpp"]:
    if needle not in stock_screen_cpp:
        missing_ui.append(f"StockScreen.cpp: {needle}")
for needle in required_ui_wiring["WeatherScreen.cpp"]:
    if needle not in weather_screen_cpp:
        missing_ui.append(f"WeatherScreen.cpp: {needle}")

if missing or present_forbidden or missing_native or missing_landscape or missing_ui:
    if missing:
        print("missing TFT contract flags:")
        for flag in missing:
            print(f"  {flag}")
    if missing_native:
        print("missing native-test wiring:")
        for item in missing_native:
            print(f"  {item}")
    if present_forbidden:
        print("forbidden TFT contract flags:")
        for flag in present_forbidden:
            print(f"  {flag}")
    if missing_landscape:
        print("missing landscape contract:")
        for item in missing_landscape:
            print(f"  {item}")
    if missing_ui:
        print("missing UI wiring contract:")
        for item in missing_ui:
            print(f"  {item}")
    sys.exit(1)

print("T-Display-S3 TFT + 320x170 + multi-app native/UI wiring contract: OK")
