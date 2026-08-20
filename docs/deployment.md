# T-Display GP Deployment

## Fixed responsibility split

**Web ChatGPT:** GitHub source/design/code/tests, validators, native tests, real ESP32-S3 PlatformIO compile, exact-SHA Artifact verification.

**Codex:** exact-SHA Artifact download/hash check, flash prebuilt application, serial monitor, physical UI/network/idle/soak test, evidence return. Codex does not use a local rebuild as the normal deployment gate.

## Development checks

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_nixie_clock_contract.py
python tools/validate_dashboard_apps_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
```

CI additionally runs Windows native and uploads `tdisplay-gp-firmware-<SOURCE_SHA>` containing firmware.bin, partitions.bin, bootloader.bin and firmware-manifest.txt.

## Flash

Codex verifies:

```text
manifest source_sha == approved SHA
actual firmware.bin SHA256 == manifest firmware_sha256
```

Normal upgrade writes only `firmware.bin` at manifest `firmware_offset` (currently normally `0x10000`). Do not erase NVS or rewrite bootloader/partitions. Serial 115200.

## Expected firmware behavior

Startup: **NixieClock**.

Menu:

```text
股票 / 天气 / 辉光时钟 / 智能家居 / 加密货币 / 设备信息
```

Idle:

```text
Menu/Weather/HomeAssistant/Crypto/DeviceInfo: 30 s no valid button -> Nixie
Stock/Nixie: exempt
valid GPIO short/long: reset timer
network/data activity: does not reset timer
```

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

1. flash exact verified app image.
2. boot Nixie; time sync/animation correct.
3. six-app menu/input.
4. 30 s idle for five eligible views, button reset, Stock/Nixie exemptions.
5. DeviceInfo/Web.
6. Weather live/cache/UI.
7. HA client reads user's existing server; secret leak check.
8. Crypto live/cache.
9. Stock Tencent/EastMoney regression.
10. >=100 transitions, no watchdog/reboot/freeze/heap leak.

Detailed physical criteria: `docs/hardware-acceptance.md`.
