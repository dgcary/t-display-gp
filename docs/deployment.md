# T-Display GP Deployment

## Fixed responsibility split

**Web ChatGPT:** GitHub source/design/code/tests, validators, native tests, Bad Apple asset generation, real ESP32-S3 PlatformIO compile, exact-SHA Artifact verification.

**Codex:** exact-SHA Artifact download/hash check, flash prebuilt application, serial monitor, physical UI/network/navigation/soak test, evidence return. Codex does not use a local rebuild as the normal deployment gate.

## Development checks

Bad Apple asset generation requires ffmpeg. CI installs it explicitly.

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_nixie_clock_contract.py
python tools/validate_dashboard_apps_contract.py
python tools/validate_bad_apple_contract.py
pio test -e native
python tools/prepare_bad_apple_asset.py
pio run -e lilygo-t-display-s3
```

The asset generator downloads a pinned conversion source only when `.badapple-cache/source.mp4` is absent, verifies its Git blob SHA1, generates 168×126 / 10 FPS / 2190-frame packed data, verifies every delta round-trip, and writes ignored `src/generated/BadAppleAsset.*` files for the ESP build. The source MP4 is not part of the repository or deployment artifact.

CI additionally runs Windows native and uploads `tdisplay-gp-firmware-<SOURCE_SHA>` containing firmware.bin, partitions.bin, bootloader.bin and firmware-manifest.txt.

## Flash

Codex verifies:

```text
manifest source_sha == approved SHA
actual firmware.bin SHA256 == manifest firmware_sha256
```

Normal upgrade writes only `firmware.bin` at manifest `firmware_offset` (currently normally `0x10000`). The Bad Apple asset is already compiled into firmware.bin; there is no separate media/filesystem flash step. Do not erase NVS or rewrite bootloader/partitions. Serial 115200.

## Expected firmware behavior

Startup: **NixieClock**.

Menu:

```text
股票 / 天气 / 辉光时钟 / 智能家居 / 加密货币 / 设备信息
```

Navigation:

```text
No automatic idle-to-Nixie switch.
Menu/Stock/Weather/Nixie/HomeAssistant/Crypto/DeviceInfo remain active until explicit button navigation.
```

## Weather / Bad Apple

Open-Meteo network behavior is unchanged and still retains current + 3-day structured data. The visible Weather layout is now:

```text
left: current condition/temp/apparent/humidity/wind/rain/update + compact 今/明 rows
right: x=152..319, y=27..152, Bad Apple 168×126 monochrome video
```

The day-after forecast is intentionally not rendered. The Weather screen does not draw the previous full-width header divider or the vertical separator beside the video, so the Bad Apple viewport stays visually clean. Bad Apple playback is 2190 frames at 10 FPS (~219 s), silent and looping. It restarts at frame 0 when Weather is entered, redraws only the video viewport at frame cadence, and stops video updates immediately after leaving Weather. Playback uses local flash data and requires no runtime network/task/worker.

The project/source-code license does not relicense the underlying Bad Apple!! PV/music; original media rights remain with their respective rights holders.

## Home Assistant deployment/config

**Use the user's existing Home Assistant server. T-Display is only a REST client.**

T-Display HA-client configuration page:

```text
http://<device-ip>:8081/
```

This page is not an HA server. Configure existing HA Base URL, Long-Lived Access Token, 1–4 entity IDs, optional labels, refresh 30–300 s and CA only when using HTTPS.

HTTP examples:

```text
http://homeassistant.local:8123
http://<ha-ip>:8123
```

HTTP requires no CA but sends Bearer Token cleartext on LAN; trusted LAN only.

HTTPS requires PEM CA and uses strict `setCACert`, never insecure fallback.

## Crypto

Binance market-data-only `data-api.binance.vision`, BTCUSDT/ETHUSDT/SOLUSDT, no API key, 60 s active-only refresh.

## Serial evidence

```text
[md] Stock
[appdata] WEATHER / HOME_ASSISTANT / CRYPTO
[net] HA mode=HA_HTTP or HA_CA
[sys] MENU/STOCK/WEATHER/NIXIE_CLOCK/HOME_ASSISTANT/CRYPTO/DEVICE_INFO
```

Never expose HA Token.

## Codex smoke

1. flash exact verified app image only at manifest application offset.
2. boot Nixie; time sync/animation correct.
3. six-app menu/input.
4. leave Menu/Weather/HA/Crypto/DeviceInfo/Stock on screen for >60 s and confirm there is no automatic app switch.
5. enter Weather: current data readable; only 今/明 forecasts appear; right-side Bad Apple fills 168×126; no full-width top divider or vertical video separator; playback progresses near 10 FPS with no whole-screen flicker.
6. keep Weather open for ~220 s and confirm the full sequence loops; leave Weather and confirm video redraw stops; re-enter and confirm it restarts from beginning.
7. Weather live/cache/network regression.
8. DeviceInfo/Web.
9. HA client reads user's existing server; secret leak check.
10. Crypto live/cache.
11. Stock Tencent/EastMoney regression.
12. >=100 transitions, no watchdog/reboot/freeze/heap leak.

Detailed physical criteria: `docs/hardware-acceptance.md`.
