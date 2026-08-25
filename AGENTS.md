# AGENTS.md — T-Display GP

GitHub `dgcary/t-display-gp` is the source of truth for this firmware.

## Target

- LILYGO T-Display-S3 only; ESP32-S3.
- ST7789 physical 170×320, 8-bit parallel.
- logical **320×170 landscape rotation 3**.
- Arduino/C++17 via PlatformIO env `lilygo-t-display-s3`.

Do not silently change target/pins/display/orientation.

## Development / deployment split

Web ChatGPT owns source inspection, design, implementation, regression tests, GitHub commits/PR updates, validators, native tests, real ESP32-S3 PlatformIO compile, exact-SHA artifact verification.

Required development checks:

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

`prepare_bad_apple_asset.py` requires ffmpeg plus access to its pinned source when `.badapple-cache/source.mp4` is absent. It verifies the source Git blob SHA1 and the complete generated delta round-trip before emitting ignored `src/generated/BadAppleAsset.*` build inputs.

CI publishes `tdisplay-gp-firmware-<SOURCE_SHA>` containing firmware.bin, partitions.bin, bootloader.bin, firmware-manifest.txt.

Codex only downloads exact artifact, verifies manifest/hash, flashes application image, monitors serial and performs physical tests. Normal deployment does not recompile, erase NVS, or rewrite bootloader/partitions.

## Hardware/input

- GPIO15 display power HIGH before TFT init.
- GPIO38 backlight.
- GPIO0/GPIO14 INPUT_PULLUP active-low.
- debounce 40 ms, long 700 ms, long suppresses release-short, no hold repeat.

```text
normal app: GPIO0 short prev; GPIO14 short next; GPIO0 long menu; GPIO14 long no-op
menu:       GPIO0 short prev; GPIO14 short next; GPIO0 long no-op; GPIO14 long enter
```

## Current App shell

```text
StockApp
WeatherApp
NixieClockApp
HomeAssistantApp
CryptoApp
DeviceInfoApp
```

### Startup / navigation

**Startup = `NIXIE_CLOCK`.**

There is **no automatic idle-to-Nixie behavior**.

- MENU / STOCK / WEATHER / NIXIE_CLOCK / HOME_ASSISTANT / CRYPTO / DEVICE_INFO remain active indefinitely until explicit user navigation.
- `AppManager` must not maintain a global inactivity timer or switch apps because of elapsed idle time.
- network/data activity never changes the active app.

## App lifecycle

- `main.cpp` only common boot/service/AppManager wiring; app logic stays in app/controller/provider boundaries.
- only active app gets normal input/tick/render.
- app exit preserves valid cache/state.
- inactive late results never redraw TFT.
- menu must not hard-code exact count behavior.

## Stock

- Tencent quote + intraday primary; EastMoney secondary/fallback.
- quote and intraday health independent.
- quote traffic outranks intraday; waiting intraday latest-wins.
- every new intraday cycle starts Tencent.
- Tencent transient intraday retry bounded/deferred max 3 and yields to quote.
- EastMoney intraday failure terminal for cycle; full cycle failure waits normal refresh.
- quote failover after 3 Tencent failures in existing window; while secondary probe Tencent at existing interval; 2 consecutive successes recover.
- Stock exit pauses new/pending MarketDataWorker execution but does not force-kill already executing HTTPS.
- Stock remains active until the user explicitly leaves it.
- parsers remain fail-closed; failures preserve cache.

## Weather

- Open-Meteo V1; provider keeps current + 3-day structured data.
- default 15 min; 5–60 min configurable.
- schedule from last attempt, no tight retry.
- failure preserves cache and cannot poison Stock health.
- remote fetch is active-only through the existing shared AppDataWorker.
- Weather UI shows current data plus compact **Today/Tomorrow only**; `dayAfter` may remain in provider/cache but must not be rendered.
- right-side Bad Apple viewport is fixed **x=152, y=27, 168×126**.
- do not draw a full-width header divider or a vertical left/video separator; the Bad Apple viewport and its boundary must remain free of UI divider lines.
- Bad Apple playback contract: **2190 frames, 10 FPS, ~219 s, 1-bit monochrome, silent, loop**.
- Weather entry resets playback to frame 0. Weather exit stops animation scheduling/rendering.
- 10 FPS updates redraw only the video viewport; do not full-screen clear at frame cadence.
- playback is local flash data only: no new task, AppDataWorker request, NetworkArbiter acquisition, HTTP or TLS.
- runtime keeps only one 1-bit frame buffer (2646 bytes) plus a small RGB565 row buffer.
- generated media uses first-frame + XOR sparse delta encoding. Build-time conversion is pinned by source commit and Git blob SHA1 and must verify every encoded frame round-trip.
- original MP4 and generated `BadAppleAsset.*` are build inputs/cache and are not committed.
- repository/source-code licensing does not grant rights to the underlying Bad Apple!! PV/music; media rights remain with their respective holders.

## Nixie

- default startup; not an automatic idle destination.
- local-only: no HTTPClient/WiFiClientSecure/HttpTransport/AppDataWorker/NetworkArbiter dependency.
- reuse common system clock via `DeviceLayer::localDateTime()`; no independent NTP client/task.
- valid HH:MM/date/weekday/seconds; invalid time explicit waiting.
- ~1 s sample, ~500 ms colon phase; partial refresh, not periodic full-screen clear.

## DeviceInfo

Local-only. Show IP, SSID/RSSI/MAC、uptime/time、heap/min heap、PSRAM and `Web: http://<IP>/`; no password/token display.

## Home Assistant

**The user's existing Home Assistant is the server. T-Display-S3 is only a REST API client. Do not implement a second HA server on the board.**

V1:

- read-only, no `/api/services` writes/control.
- 1–4 entity IDs, optional labels.
- sequential `GET <base_url>/api/states/<entity_id>`.
- `Authorization: Bearer <Long-Lived Access Token>`.
- refresh 30–300 s, default 30; active-only.
- per-entity last-valid cache survives errors.
- HA config stored in separate `ha_config` NVS blob; AppConfig remains schema v2.
- T-Display client config page `http://<device-ip>:8081/`; this is not HA server.
- status may expose `ha_token_set` / `ha_ca_set`, never Token/CA contents.
- serial never logs Token/full Authorization.

Transport:

```text
http://  -> WiFiClient; trusted-LAN cleartext mode; CA not required
https:// -> WiFiClientSecure + configured setCACert(); setInsecure forbidden
```

Unknown schemes invalid. HTTPS without valid CA invalid. Both modes acquire NetworkArbiter, reuse false, bounded HA body 4 KiB. HTTP explicitly exposes Bearer token to LAN observers and must be documented as trusted-LAN only.

## Crypto

- BTCUSDT / ETHUSDT / SOLUSDT.
- provider `data-api.binance.vision`, `/api/v3/ticker/24hr`, one request for 3 symbols.
- no Binance API credential.
- fixed V1 refresh 60 s; active-only.
- parser maps by symbol, rejects unknown/duplicate/missing/malformed rows; update only after all 3 validate.
- failure preserves last complete snapshot.

## Shared AppDataWorker

Do not create per-app workers.

- Stock keeps specialized MarketDataWorker.
- Weather / HA / Crypto share exactly one AppDataWorker request queue/task.
- typed result queues by AppDataRequestType prevent cross-app delayed-result loss.
- FreeRTOS queues pass pointers to C++ request/results; do not raw byte-copy objects containing std::string.
- Nixie/DeviceInfo use no network worker.
- Bad Apple playback is not a worker and never adds requests to AppDataWorker.

## NetworkArbiter / transport

All actual external HTTP/TLS operations serialize through shared NetworkArbiter, max one at once.

Public Stock/Weather/Crypto HttpTransport remains:

- connect 1500 ms.
- HTTP/read setting 2500 ms.
- TLS handshake 5 s cap.
- retained body max 32 KiB.
- HTTPClient reuse false.
- don't pass millisecond read constant to seconds-based WiFiClientSecure::setTimeout in Arduino-ESP32 2.0.14.
- public-data clients keep existing setInsecure trust model unless separately redesigned.

HA HTTPS credential path is the exception: CA verification required, no setInsecure.

## Config

- AppConfig schema v2, namespace `stockticker`.
- v1 migration preserves stocks/names/refresh; Weather default disabled/15 min.
- Nixie adds no config fields.
- HA separate `ha_config` blob.
- Bad Apple adds no runtime config/NVS fields.
- normal firmware upgrade preserves NVS.
- configuration changes reboot-apply atomically.

## Diagnostics

```text
[md]      Stock
[appdata] WEATHER / HOME_ASSISTANT / CRYPTO
[net]     actual transport; HA mode=HA_HTTP or HA_CA
[sys]     MENU|STOCK|WEATHER|NIXIE_CLOCK|HOME_ASSISTANT|CRYPTO|DEVICE_INFO
```

No full bodies/credentials by default.

## Physical acceptance

Real T-Display-S3 evidence must verify:

- 320×170 + Chinese.
- boot Nixie.
- Nixie local time/partial animation/no network.
- six-app menu/input.
- every app/menu remains on the selected view during >60 s inactivity; no automatic switch to Nixie.
- Weather left text remains readable; Today/Tomorrow are shown and day-after is absent.
- Weather has no full-width top divider and no vertical divider at the Bad Apple boundary.
- Weather right 168×126 Bad Apple animation progresses near 10 FPS, shows multiple recognizable silhouette scenes, loops around 219 s, and causes no 10 FPS full-screen flicker.
- leaving Weather stops Bad Apple redraw; re-entering Weather restarts at frame 0.
- HA connects as client to existing user server over configured HTTP or CA-verified HTTPS; 1–4 entities render; no secret leak.
- Crypto BTC/ETH/SOL live.
- Stock Tencent/EastMoney behavior unchanged.
- caches survive transitions/failures.
- >=100 transitions, no watchdog/reboot/freeze/monotonic heap leak.

See `docs/hardware-acceptance.md`.

## Safety

Scope is this repository and connected T-Display-S3. Do not modify unrelated servers/network/DHCP/Wi-Fi infrastructure. Never hard-code passwords, GitHub secrets, HA tokens or account credentials.

When lifecycle/input/config/provider/transport/build/deployment/UI changes, keep README.md, AGENTS.md, docs/deployment.md, docs/api-contract.md and docs/hardware-acceptance.md aligned in the same PR.
