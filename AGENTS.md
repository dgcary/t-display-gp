# AGENTS.md — T-Display GP

This repository is the source of truth for T-Display GP firmware. These rules apply to ChatGPT, Codex and other automated agents.

## Target

- Repository: `dgcary/t-display-gp`
- Hardware: **LILYGO T-Display-S3 only**
- MCU: ESP32-S3
- TFT: ST7789 physical 170×320, 8-bit parallel
- Logical orientation: **320×170 landscape, rotation 3**
- Framework: Arduino/C++17 via PlatformIO
- Environment: `lilygo-t-display-s3`

Do not silently retarget board, display, controller, pinout or orientation.

## Source of truth and workflow

GitHub exact SHA is authoritative. Do not use an old local checkout/binary as source of truth.

Web ChatGPT owns source inspection, requirements/design, implementation, tests, review, GitHub changes, PlatformIO validation, real ESP32-S3 compilation and verified firmware artifacts.

Required verification:

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_nixie_clock_contract.py
python tools/validate_dashboard_apps_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
```

CI must publish `tdisplay-gp-firmware-<SOURCE_SHA>` containing:

```text
firmware.bin
partitions.bin
bootloader.bin
firmware-manifest.txt
```

Codex is the local hardware executor only: download exact-SHA artifact, verify manifest/hash, identify the connected T-Display-S3, flash the prebuilt application image, monitor serial, perform physical tests, and return evidence. Do not use Codex local compilation as the normal deployment gate.

For the unchanged partition layout, normal upgrade writes only `firmware.bin` at the manifest offset (currently `0x10000`). Do not erase flash/NVS or rewrite bootloader/partitions unless separately approved.

## Hardware/input invariants

- GPIO15 display power HIGH before TFT initialization.
- GPIO38 backlight.
- GPIO0/GPIO14 INPUT_PULLUP, active-low.
- TFT color order `TFT_RGB`, init `INIT_SEQUENCE_3`, rotation 3.
- debounce 40 ms.
- long press 700 ms.
- long press fires once and suppresses short-on-release.
- no hold auto-repeat in V1.

Input semantics:

```text
normal app:
  GPIO0 short  -> app previous
  GPIO14 short -> app next
  GPIO0 long   -> main menu
  GPIO14 long  -> reserved/no-op

menu:
  GPIO0 short  -> previous app
  GPIO14 short -> next app
  GPIO0 long   -> no-op
  GPIO14 long  -> enter selected app
```

Any valid non-NONE input event counts as user activity for the idle policy, including a reserved/no-op long event.

## Current shell and startup/idle policy

Current registry:

```text
StockApp
WeatherApp
NixieClockApp
HomeAssistantApp
CryptoApp
DeviceInfoApp
```

**Startup defaults to `NIXIE_CLOCK`.**

`AppManager` owns the global idle policy:

- `MENU`, `WEATHER`, `HOME_ASSISTANT`, `CRYPTO`, `DEVICE_INFO`: after **30,000 ms** without a valid button event, switch to `NIXIE_CLOCK`.
- `STOCK`: exempt from auto-idle indefinitely.
- `NIXIE_CLOCK`: exempt; it is the idle destination.
- network/data refreshes do not reset user activity.
- elapsed time must use unsigned wrap-safe `millis()` subtraction.
- the idle switch occurs before the old app receives another `tick()` at the boundary, so a remote app does not launch an extra request at 30 seconds.

Do not duplicate idle timers inside individual apps.

## App architecture

- `main.cpp` owns common boot/provisioning and drives `AppManager`; app business logic belongs in app/controller/provider boundaries.
- Only the active app receives normal input/tick/render calls.
- Leaving an app preserves its valid cache/state.
- Inactive apps never redraw the TFT after late network completion.
- Menu behavior must not assume an exact app count.

### StockApp

- Keep existing `StockController`, `StockScreen`, `MarketDataWorker`, Tencent/EastMoney policy.
- GPIO0/GPIO14 short presses remain previous/next stock.
- Stock is exempt from the global 30-second idle fallback.
- Exiting Stock pauses new MarketDataWorker execution; do not force-kill an already executing HTTPS request.
- Returning to Stock preserves cache and existing QoS/refresh behavior.

### WeatherApp

- V1 provider Open-Meteo.
- Default refresh 15 minutes; configured 5–60 minutes.
- Scheduling anchored to last attempt to prevent retry storms.
- Preserve last successful cache on failure.
- Weather failure cannot affect stock health.
- Active-only scheduling.
- Hand-painted watercolor cat remains lightweight/procedural and bounded to its region; animation-only frames must not clear the full screen.

### NixieClockApp

- Default startup and global idle destination.
- Local-only: no `HTTPClient`, `WiFiClientSecure`, `HttpTransport`, `AppDataWorker` or `NetworkArbiter` dependency.
- Reuse `DeviceLayer::localDateTime()` and common NTP/system clock; do not add a separate NTP task/client.
- Show valid local HH:MM, date, weekday and seconds; invalid/unsynchronized time fails closed with explicit waiting state.
- Approx. 1 s local-time sampling and approx. 500 ms colon phase.
- Animation-only refresh must be bounded/partial; do not clear the entire TFT every 500 ms.
- Short presses currently no-op; GPIO0-long menu remains global.

### DeviceInfoApp

- Local-only; no external requests.
- Show LAN IP prominently plus SSID/RSSI/MAC, uptime/time, heap/min-heap, PSRAM and `Web: http://<IP>/`.
- Never display passwords, HA tokens or other secrets.

### HomeAssistantApp

**The existing Home Assistant installation is the server. T-Display-S3 is only a REST API client.** Do not run or emulate a Home Assistant server on the device.

V1 contract:

- read-only; no `/api/services` writes or entity control.
- 1–4 configured entity IDs, optional display labels.
- sequential `GET <base_url>/api/states/<entity_id>`.
- `Authorization: Bearer <Long-Lived Access Token>`.
- refresh 30–300 seconds, default 30 seconds.
- active-only scheduling; per-entity last-valid cache survives errors.
- configuration stored separately in `ha_config` under the existing NVS namespace; do not change AppConfig schema v2 solely for HA.
- device config portal: `http://<device-ip>:8081/`; it is a T-Display config UI, **not** a HA server.
- `/api/ha/status` may expose `ha_token_set` / `ha_ca_set` booleans but never Token or CA contents.
- serial logs must never include Token or full Authorization header.

Transport modes:

```text
http://...
  -> ordinary WiFiClient
  -> intended only for a trusted LAN
  -> Bearer token is cleartext on the LAN

https://...
  -> WiFiClientSecure
  -> configured CA PEM via setCACert()
  -> setInsecure() is forbidden
```

Both modes go through `NetworkArbiter`, use bounded response retention, and keep normal connect/read/reuse limits. Unknown URL schemes are rejected. HTTPS without CA is invalid.

### CryptoApp

- BTCUSDT, ETHUSDT, SOLUSDT.
- Provider: Binance market-data-only `data-api.binance.vision` `/api/v3/ticker/24hr` with all 3 symbols in one request.
- No API key/credential.
- fixed V1 refresh 60 seconds.
- active-only scheduling.
- parser maps by symbol rather than response order, rejects duplicate/missing/malformed values, and updates cache only after all 3 validate.
- failure preserves last complete snapshot.

## Shared low-frequency app data

Do not create one FreeRTOS worker per app.

- Stock keeps specialized `MarketDataWorker`.
- Weather / Home Assistant / Crypto share exactly **one** `AppDataWorker` request path.
- Results are separated by `AppDataRequestType` so one app cannot consume another app's delayed result.
- FreeRTOS queues pass pointers to C++ request/result objects; do not raw byte-copy non-trivial objects containing `std::string`.
- Local-only Nixie/DeviceInfo do not use either network worker.

## Market-data invariants

Tencent is quote + intraday primary; EastMoney is quote + intraday secondary/fallback.

- quote and intraday provider health are independent.
- every new intraday cycle starts Tencent regardless of quote provider.
- Tencent transient intraday retries are bounded/deferred, max 3 attempts, and yield to quote work.
- EastMoney intraday failure is terminal for that cycle.
- full intraday failure waits normal refresh; no empty-cache retry storm.
- quote traffic outranks intraday; waiting intraday is latest-wins.
- quote failover: 3 Tencent failures in existing window -> EastMoney; while on EastMoney probe Tencent at existing interval; 2 consecutive probe successes recover.
- parser validation stays fail-closed.

## Network/memory invariants

Actual external HTTP/TLS operations are serialized through `NetworkArbiter`: at most one executes at a time.

Public-data `HttpTransport` constraints remain:

- connect timeout 1500 ms.
- HTTP/read timeout setting 2500 ms.
- TLS handshake cap 5 s.
- retained body max 32 KiB.
- `HTTPClient::setReuse(false)`.
- do not pass the 2500-ms read constant directly to the seconds-based Arduino-ESP32 2.0.14 `WiFiClientSecure::setTimeout()`.
- public market/weather/crypto clients keep the existing `setInsecure()` trust model unless separately redesigned.

Home Assistant is a credentialed exception: HTTPS must use configured CA and must never use `setInsecure()`; HTTP is an explicit trusted-LAN cleartext mode. HA retained body is bounded at 4 KiB.

## Configuration invariants

- AppConfig schema remains **v2** in `stockticker` namespace.
- v1 migration preserves stock symbols/names/refresh and adds disabled 15-minute weather defaults.
- Nixie adds no config fields.
- Home Assistant uses separate `ha_config` blob; normal firmware upgrade must preserve it.
- configuration updates are reboot-applied; do not partially hot-apply a mixed state.

## Diagnostics

```text
[md]      market requests
[appdata] WEATHER / HOME_ASSISTANT / CRYPTO
[net]     actual network timing; HA mode=HA_HTTP or HA_CA
[sys]     runtime resources and active app
```

`[sys] app=` values include:

```text
MENU | STOCK | WEATHER | NIXIE_CLOCK | HOME_ASSISTANT | CRYPTO | DEVICE_INFO
```

Do not log full provider bodies or credentials by default.

## UI invariants

- 320×170 logical landscape.
- Stock positive red / negative green.
- Stock lunch discontinuity and previous-close/open references remain.
- Stock footer distinguishes quote (`Q`) and intraday (`I`) sources.
- Menu/Weather/Nixie/HomeAssistant/Crypto/DeviceInfo use bounded text/simple shapes; no large bitmap/GIF additions without memory review.
- Chinese text uses the Unicode font path where required.

## Physical acceptance

At minimum verify on the actual board:

1. correct 320×170 orientation and Chinese text.
2. boot enters NixieClock.
3. Nixie shows real local HH:MM/date/weekday/seconds after sync, with no periodic full-screen flicker and no network traffic caused by Nixie.
4. menu navigates all six apps and GPIO14-long enters selected app.
5. Weather, Home Assistant, Crypto, DeviceInfo and Menu return to Nixie at 30 seconds with no valid button event.
6. a valid button event before 30 seconds restarts the idle timer.
7. Stock remains on Stock beyond 30 seconds; Nixie remains Nixie.
8. Home Assistant connects as a client to the user's existing server using the configured HTTP or verified-HTTPS mode; 1–4 entities render correctly; no Token appears in logs/status.
9. Crypto retrieves BTC/ETH/SOL data from Binance and displays price/24h change.
10. Weather retrieves configured location data and watercolor-cat rendering remains stable.
11. Stock Tencent primary/fallback semantics remain correct.
12. caches survive transitions/failures.
13. at least 100 multi-app transitions without watchdog/reboot/freeze and without monotonic heap leakage.

See `docs/hardware-acceptance.md` for the detailed checklist.

## Scope/safety

Deployment is limited to the connected T-Display-S3 and this repository. Do not modify unrelated servers, routers, DHCP, Wi-Fi infrastructure, or other devices. Never hard-code Wi-Fi passwords, GitHub credentials, API secrets, broker credentials, HA tokens or personal account data.

When lifecycle/input/config/provider/transport/build/deployment/UI behavior changes, keep these aligned in the same PR:

- `README.md`
- `AGENTS.md`
- `docs/deployment.md`
- `docs/api-contract.md`
- `docs/hardware-acceptance.md`
