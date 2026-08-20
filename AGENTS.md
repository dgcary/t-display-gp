# AGENTS.md — T-Display GP

This repository is the source of truth for the T-Display GP firmware.
These instructions apply to ChatGPT, Codex and other automated development/deployment agents.

## Target

- Repository: `dgcary/t-display-gp`
- Hardware: **LILYGO T-Display-S3 only**
- MCU: ESP32-S3
- Physical TFT: ST7789 170×320, 8-bit parallel
- Application orientation: **320×170 landscape, rotation 3**
- Framework: Arduino/C++17 via PlatformIO
- Primary environment: `lilygo-t-display-s3`

Do not silently retarget hardware, display revision, controller, pinout, or orientation.

## Source-of-truth rule

GitHub is the source of truth. Do not treat an old local checkout, binary, or prior chat artifact as authoritative.

For normal deployment use latest approved `main`. If the user requests a feature branch or exact SHA, deploy exactly that ref and record it.

## Fixed development / deployment workflow

This project uses a strict split of responsibilities.

### Web ChatGPT / development side

Web ChatGPT owns:

1. fetch/inspect the latest GitHub source,
2. requirements/design,
3. source-code changes,
4. regression tests and review,
5. GitHub commits/PR updates,
6. PlatformIO validators/native tests,
7. real ESP32-S3 firmware compilation,
8. publishing a verified prebuilt firmware artifact.

Required development verification:

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
```

The CI build must publish a GitHub Actions artifact named:

```text
tdisplay-gp-firmware-<SOURCE_SHA>
```

containing at least:

```text
firmware.bin
partitions.bin
bootloader.bin
firmware-manifest.txt
```

The manifest records the exact source SHA, firmware offset and SHA256 hashes.

### Codex / local hardware side

Codex is **not** the normal development/compiler for this project.

Codex only owns:

1. download the prebuilt artifact for the exact approved SHA,
2. verify the manifest/source SHA and firmware SHA256,
3. identify the connected T-Display-S3 port,
4. flash the already-built `firmware.bin`,
5. serial monitoring,
6. physical UI/input/network/soak testing,
7. report physical evidence back to Web ChatGPT.

Codex must **not** run `pio run` or perform normal compile/debug work as a deployment gate. A local Windows PlatformIO build failure is not a reason to rebuild/fix locally when a verified prebuilt artifact exists.

For the current unchanged partition layout, hardware deployment writes only the application image at the manifest-provided offset (currently `0x10000`), preserving bootloader/partition/NVS data. Do not erase flash or NVS unless a separately approved test explicitly requires it.

If physical testing finds a bug, Codex reports reproduction/logs/screenshots; Web ChatGPT changes the code, recompiles, republishes a new artifact, and Codex flashes that new exact SHA.

Rules:

1. Never flash an artifact whose manifest `source_sha` differs from the approved Git SHA.
2. Verify `firmware.bin` SHA256 against the manifest before flashing.
3. Do not claim hardware PASS without physical-board evidence.
4. Record exact source SHA, Actions run/artifact, port and test result for each hardware acceptance run.
5. Do not modify source code, tests, PR state, or `main` during Codex hardware execution.

## Hardware and input invariants

- GPIO15: display power, HIGH before TFT initialization
- GPIO38: backlight
- GPIO0/GPIO14: INPUT_PULLUP, active low
- TFT color order: `TFT_RGB`
- TFT init: `INIT_SEQUENCE_3`
- Application rotation: `tft_.setRotation(3)` / logical 320×170
- debounce: 40 ms
- long press: 700 ms
- long press fires once and suppresses short-on-release
- no hold auto-repeat in V1

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

Run `tools/validate_tdisplay_setup.py` for TFT/build/UI/artifact-contract changes.

## Multi-app architecture invariants

- `main.cpp` owns common boot/provisioning and drives `AppManager`; it must not accumulate app-specific business logic.
- Each app owns its controller/screen/service boundary and follows enter/exit/input/tick/render semantics.
- Only the active app receives normal input/tick/render calls.
- Exiting an app preserves valid cache/state; do not reconstruct an app on every switch.
- App registry/menu must remain extensible; do not hard-code menu behavior around an exact app count.
- Startup defaults to StockApp unless a separately approved configuration feature changes it.
- Current shell includes StockApp, WeatherApp and DeviceInfoApp.
- Future AirQuality/HomeAssistant/ServiceMonitor apps should use the same shell rather than bypassing AppManager.

## DeviceInfoApp invariants

- DeviceInfoApp is local-only; it must not create network requests.
- It exposes the current LAN IP prominently so the user can reach the existing Web configuration page.
- It may show SSID, RSSI, MAC, uptime, local time, free/min heap and PSRAM state.
- Device diagnostics must never display Wi-Fi passwords, API tokens, Home Assistant tokens, or other secrets.

## StockApp invariants

- Existing `StockController`, `StockScreen`, `MarketDataWorker`, Tencent/EastMoney behavior remain the stock source of truth.
- GPIO0/GPIO14 short presses must preserve previous/next-stock behavior.
- When StockApp exits, pause new MarketDataWorker execution; do not force-kill an in-flight HTTPS request.
- Returning to StockApp preserves cache and resumes existing QoS/refresh behavior.
- Quote provider and intraday provider are independent and must be observable separately in the footer/diagnostics.

## Market-data architecture invariants

- Main/UI never performs blocking market HTTP.
- **Tencent is quote + intraday primary.**
- **EastMoney is quote + intraday secondary/fallback.**
- Quote failover and intraday failover are independent; intraday success/failure must not alter quote-provider health.
- Every new intraday cycle starts with Tencent even when quote traffic is temporarily on EastMoney.
- Tencent transient intraday errors use the existing bounded/deferred retries before falling back to EastMoney.
- EastMoney intraday failure never recursively falls back again.
- A fully failed intraday cycle waits the normal intraday refresh interval before opening another cycle; no empty-cache retry storm.
- Quote/intraday failures preserve the last valid caches and have independent health state.
- Quote traffic outranks intraday.
- Waiting intraday is latest-wins.
- Intraday transient retry is bounded/deferred: max 3 Tencent attempts; it must yield to quote traffic.
- Existing approved request TTLs remain unchanged unless separately designed.
- Do not weaken parser validation to accept unknown malformed market payloads.
- Quote primary failover: 3 Tencent failures within the existing failure window switch to EastMoney; while on EastMoney, probe Tencent at the existing interval and require 2 Tencent probe successes to recover.

## WeatherApp invariants

- Weather UI/controller depend on `IWeatherProvider`, not raw Open-Meteo JSON.
- V1 provider is `OpenMeteoProvider`.
- Default refresh is 15 minutes; configured range is 5–60 minutes.
- Refresh pacing is anchored to the last request attempt, preventing retry storms after failures.
- Last successful weather snapshot remains visible on provider/network/parse failure.
- Weather failure affects WeatherApp only and must not poison stock health.
- Raw weather JSON is discarded after strict parse into bounded structured state.
- WeatherApp does not actively schedule when it is not the active app.
- Weather pet style is `HAND_PAINTED_WATERCOLOR`; keep it lightweight/procedural and bounded to the pet region.
- Animation-only frames redraw only the bounded pet region, not the full TFT.
- Weather condition text shown in Chinese must use the Unicode font path, including the three-day forecast cards.

## Shared network / memory invariants

External HTTP/TLS is serialized through `NetworkArbiter`:

> At most one external HTTP/TLS request executes at a time.

Keep these transport constraints:

- connect timeout = 1500 ms
- HTTP/read timeout setting = 2500 ms
- TLS handshake cap = 5 s
- maximum retained body = 32 KiB
- `HTTPClient::setReuse(false)`
- do not pass the millisecond read constant directly to the seconds-based `WiFiClientSecure::setTimeout()` in Arduino-ESP32 2.0.14
- current public-data clients retain the existing `setInsecure()` behavior; do not extend that trust model to future sensitive credentials without a separate security design

Do not create one FreeRTOS worker per new app. Stock keeps its specialized MarketDataWorker; low-frequency app data uses the shared AppDataWorker path.

Run `tools/validate_http_transport_contract.py` for any transport/arbiter change.

## Configuration invariants

- Current schema is v2.
- Keep existing NVS namespace `stockticker` in this milestone.
- v1 config migration must preserve stock symbols, display names, and quote refresh.
- Weather defaults after v1 migration: disabled, 15-minute refresh.
- Weather location is shared device location data so future AirQualityApp can reuse it.
- Captive Portal and LAN Web configuration must use the same `ProvisioningForm` validation path.
- Configuration updates remain atomic/reboot-applied; do not partially hot-apply only some modules.

## Diagnostics

Market requests: `[md]`

Low-frequency app data: `[appdata]`

Runtime resources every ~60 seconds: `[sys]`, including active app, free/min heap, PSRAM totals/free, and main task stack high-water mark.

Intraday fallback should emit:

```text
[md] id=... type=INTRADAY symbol=... fallback=TX->EM
```

When diagnosing a live issue, preserve the relevant request line plus surrounding `[sys]` lines. Do not log full response bodies or credentials by default.

## UI invariants

- Logical layout remains 320×170 landscape.
- Stock positive change = red; negative = green.
- Stock lunch discontinuity and `昨收`/`今开` references remain.
- Stock footer distinguishes quote (`Q`) and intraday (`I`) source when intraday data exists.
- Menu/Weather/DeviceInfo use bounded text/simple shapes; do not add large bitmap/GIF assets without a memory review.
- Weather companion uses the approved hand-painted watercolor look through layered low-saturation primitives; do not regress to the old single-color geometric orange cat.
- Inactive apps must never redraw the TFT after a late network completion.

## Provider caution

Public market/weather endpoints may change. For any provider failure:

1. capture real device diagnostics first,
2. add/update regression data or behavior test before parser/provider changes,
3. change provider/transport boundary where possible,
4. never relax strict parsing merely to raise apparent success rate.

See `docs/api-contract.md`.

## Physical acceptance

At minimum verify on the actual T-Display-S3:

- correct 320×170 orientation and Chinese text,
- boot enters StockApp,
- short stock buttons still work,
- long GPIO0 enters menu without also switching stock,
- menu short navigation and GPIO14-long enter work,
- DeviceInfo shows the real LAN IP and `http://<IP>/` is reachable from the LAN,
- WeatherApp retrieves configured location data,
- weather colors, three-day Chinese condition text and two-frame hand-painted watercolor cat animation render without visible corruption/flicker,
- normal market operation prefers Tencent (`Q:TX`, and `I:TX` after intraday cache exists),
- with Tencent intraday unavailable, `[md] fallback=TX->EM` appears and EastMoney can populate/refresh the chart when healthy,
- footer reflects the actual quote/intraday source (for example `Q:TX I:TX` or fallback `Q:TX I:EM`),
- Stock/Weather caches survive menu transitions and failures,
- 100 Stock/Menu/Weather/DeviceInfo transitions with no watchdog/reboot/freeze,
- `[sys]` heap does not exhibit monotonic leakage.

Use `docs/hardware-acceptance.md` for the full checklist.

## Scope and safety

Deployment is limited to the connected T-Display-S3 and this repository. Do not modify unrelated servers, routers, DHCP, Wi-Fi infrastructure, or other devices. Never hard-code Wi-Fi passwords, GitHub credentials, API secrets, broker credentials, Home Assistant tokens, or personal account data.

## Documentation

When app lifecycle, input, configuration, provider contracts, transport, pins, build commands, artifact/deployment flow, scheduling, or UI orientation change, keep these aligned in the same PR:

- `README.md`
- `AGENTS.md`
- `docs/deployment.md`
- `docs/api-contract.md`
- `docs/hardware-acceptance.md`

A change that makes these materially wrong is incomplete.
