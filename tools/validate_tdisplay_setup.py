#!/usr/bin/env python3
"""Validate the TFT_eSPI compile-time contract for LILYGO T-Display-S3."""
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
    "+<app/StockController.cpp>",
    "+<network/EastMoneyProvider.cpp>",
    "+<network/TencentProvider.cpp>",
    "+<network/ProvisioningForm.cpp>",
    "+<ui/StockScreen.cpp>",
}
missing_native = sorted(item for item in required_native_test_wiring if item not in text)
forbidden = ["-DTFT_RGB_ORDER=TFT_BGR"]
present_forbidden = [flag for flag in forbidden if flag in text]
if missing or present_forbidden or missing_native:
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
    sys.exit(1)
print("T-Display-S3 TFT build contract: OK")
