# AGENTS.md — T-Display GP

GitHub `dgcary/t-display-gp` is the source of truth for this firmware.

## Target

- LILYGO T-Display-S3 only; ESP32-S3.
- ST7789 physical 170×320, 8-bit parallel.
- logical **320×170 landscape rotation 3**.
- Arduino/C++17 via PlatformIO env `lilygo-t-display-s3`.

Do not silently change target/pins/display/orientation.

## Development / deployment split

Web ChatGPT owns source inspection, design, implementation, regression tests, GitHub commits/PR updates, validators, native tests, real ESP32-S3 PlatformIO compile and exact-SHA artifact verification.

Required development checks:

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_app_shell_contract.py
python tools/validate_dashboard_apps_contract.py
python tools/validate_bambu_cloud_contract.py
python tools/validate_bad_apple_contract.py
pio test -e native
python tools/prepare_bad_apple_asset.py
pio run -e lilygo-t-display-s3
```

Bad Apple generation requires ffmpeg plus access to its pinned source when cache is absent. Generator must verify source Git blob SHA1 and complete frame-delta round-trip.

CI publishes `tdisplay-gp-firmware-<SOURCE_SHA>` with firmware.bin, partitions.bin, bootloader.bin and firmware-manifest.txt.

Codex only downloads exact artifact, verifies manifest/hash, flashes application image, monitors serial and performs physical tests. Normal deployment does not recompile, erase NVS or rewrite bootloader/partitions.

## Hardware / input

- GPIO15 display power HIGH before TFT init.
- GPIO38 backlight.
- GPIO0/GPIO14 INPUT_PULLUP active-low.
- debounce 40 ms, long 700 ms, long suppresses release-short, no hold repeat.

```text
normal app: GPIO0 short prev; GPIO14 short next; GPIO0 long menu; GPIO14 long no-op
menu:       GPIO0 short prev; GPIO14 short next; GPIO0 long no-op; GPIO14 long enter
```

## Current app shell

```text
StockApp
WeatherApp
BambuApp
HomeAssistantApp
DeviceInfoApp
```

Menu order is exactly:

```text
股票 / 天气 / Bambu Lab / 智能家居 / 设备信息
```

Startup = **STOCK**. There is **no automatic idle switching**. MENU / STOCK / WEATHER / BAMBU / HOME_ASSISTANT / DEVICE_INFO remain active until explicit navigation. Network/data activity never changes the active app.

`main.cpp` is common boot/service/AppManager wiring only; app business logic stays in app/controller/provider/service boundaries.

## Stock

- Tencent quote + intraday primary; EastMoney secondary/fallback.
- quote and intraday health independent.
- quote traffic outranks intraday; waiting intraday latest-wins.
- parsers fail closed and preserve last-valid cache.
- Stock keeps the dedicated `MarketDataWorker`.

## Weather / Bad Apple

- Open-Meteo V1; provider keeps current + 3-day structured data.
- default 15 min; 5–60 min configurable; active-only shared AppDataWorker request.
- failure preserves cache.
- UI shows current + Today/Tomorrow only; `dayAfter` is not rendered.
- fixed viewport x=152, y=27, 168×126.
- no full-width header divider and no vertical left/video separator.
- 2190 frames, 10 FPS, ~219 s, 1-bit monochrome, silent, loop.
- Weather entry resets playback to frame 0; exit stops animation refresh.
- redraw only video viewport; no 10 FPS whole-screen clear.
- local flash only: no playback task, HTTP, TLS, AppDataWorker request or NetworkArbiter acquisition.
- one packed frame buffer (2646 bytes) plus small RGB565 row buffer.
- original MP4 and generated asset files are build/cache inputs, not committed.
- project license does not relicense the Bad Apple!! PV/music.

## Home Assistant

The user's existing Home Assistant is the server. T-Display-S3 is only a read-only REST client; do not implement a second HA server.

- 1–4 entity IDs, optional labels.
- sequential `GET <base_url>/api/states/<entity_id>`.
- Bearer Long-Lived Access Token.
- refresh 30–300 s, default 30 s; active-only.
- per-entity last-valid cache.
- separate HA config storage.
- HTTP mode is trusted-LAN cleartext.
- HTTPS requires configured CA + `setCACert()`; `setInsecure()` forbidden in credentialed HA HTTPS.
- status may expose only secret-presence flags, never Token/CA contents.

Weather and HA share exactly **one** `AppDataWorker`. Do not create per-app workers.

## Bambu Lab Cloud

Bambu is a **device-level background integration**, not an AppDataWorker request and not active-app-only.

V1 is Cloud-first/read-only:

- no printer-LAN MQTT 8883 dependency or public exposure;
- no pause/resume/stop/light/temperature/camera control;
- account/device setup occurs through local Integrations page at `http://<device-ip>:8081/`;
- never hard-code/log/return account password or access token.

Bambu config is separate from AppConfig and HA config and persists:

```text
region
email
password (optional, for unattended renewal)
accessToken
cloudUserId
printerSerial
printerName
enabled
```

Cloud brokers:

```text
CHINA -> cn.mqtt.bambulab.com:8883
US_EU -> us.mqtt.bambulab.com:8883
```

MQTT credential/topic contract:

```text
username = cloudUserId
password = accessToken
subscribe = device/<serial>/report
```

The sole permitted V1 publish is the read-only `pushall` state-sync request to `device/<serial>/request`. No printer-control publish path may be added.

### TLS / NetworkArbiter

- Bambu login/profile/device discovery HTTPS uses CA verification. `setInsecure()` is forbidden.
- Bambu MQTT uses `WiFiClientSecure` + CA bundle. `setInsecure()` is forbidden.
- Short-lived Bambu HTTPS calls acquire the shared `NetworkArbiter` for the full request.
- MQTT connect/reconnect handshake acquires the arbiter; once connected, release it. Persistent MQTT keepalive/report traffic owns its dedicated socket and must not hold the arbiter for the lifetime of the connection.
- App transitions must never disconnect MQTT.

### MQTT service / state

- exactly one MQTT service execution context owns connect/subscribe/callback-state mutation/`mqtt.loop()`.
- `BambuMqttService` is also the sole mutable runtime owner of `BambuConfig`; UI/Portal must use mutex-protected snapshot/update APIs and must not retain mutable config references across cores.
- Portal-originated config replacement increments an external revision and is authoritative. Any background login/identity/discovery result derived from an older snapshot must verify that revision before mutating runtime config, discovered-printer cache, or NVS; stale results are discarded rather than overwriting newer user configuration.
- receive buffer target is **40960 bytes**; allocation failure is non-fatal and visible as BUFFER_ERROR.
- disconnect/network loss must preserve last valid printer snapshot.
- malformed/partial reports update only valid present fields and never destroy last valid state.
- BambuApp/BambuScreen are passive readers/renderers and must not connect/disconnect/publish.

### Token renewal

MQTT auth rc 4/5 or invalid token -> if a saved password exists, automatic HTTPS relogin -> persist replacement token/user ID -> reconnect MQTT.

Failed unattended relogin is bounded:

```text
60 s -> 300 s -> 900 s -> 1800 s -> 1800 s ...
```

If account flow requires 2FA/email code, expose a clear terminal status and stop unattended retry; never attempt to bypass the second factor.

## Integrations portal

There is exactly one `WebServer{8081}` owner: `IntegrationConfigPortal`.

Preserve HA routes:

```text
/api/ha/status
/api/ha/config
```

Bambu routes:

```text
/api/bambu/status
/api/bambu/login
/api/bambu/printers
/api/bambu/config
/api/bambu/logout
```

Blank Bambu password input preserves stored password unless the flow explicitly elects not to remember/clears it. Logout clears password, token, user ID and printer selection. Status may expose only booleans such as `password_set` / `token_set`; no raw secret values.

## DeviceInfo

Local-only. Show IP, SSID/RSSI/MAC, uptime/time, heap/min heap, PSRAM and Web address; no credentials.

## Shared workers / network concurrency

```text
Stock -> dedicated MarketDataWorker
Weather + HomeAssistant -> exactly one shared AppDataWorker
Bambu -> one persistent MQTT service/task
DeviceInfo -> local-only
Bad Apple -> local flash playback
```

All short-lived external HTTP/TLS operations serialize through `NetworkArbiter`, max one at once. Bambu persistent MQTT is the deliberate exception after connection establishment: its dedicated socket stays alive without holding arbiter.

FreeRTOS queues pass pointers to C++ objects; do not raw-copy non-trivial `std::string` objects.

## Config

- AppConfig schema v2, namespace `stockticker`.
- HA separate `ha_config` blob.
- Bambu separate `bambucloud` namespace/blob.
- Bambu runtime config updates are serialized through `BambuMqttService`; background persistence is revision-guarded against stale snapshots.
- normal firmware upgrade preserves NVS.
- configuration changes reboot-apply atomically where currently implemented.

## Diagnostics

```text
[md]      Stock
[appdata] WEATHER / HOME_ASSISTANT
[net]     actual short-lived transport; HA mode=HA_HTTP or HA_CA
[sys]     MENU|STOCK|WEATHER|BAMBU|HOME_ASSISTANT|DEVICE_INFO
```

Do not invent secret-bearing Bambu diagnostics. Password, access token, Authorization headers and full auth/device response bodies must never be logged.

## Physical acceptance

Real T-Display-S3 evidence must verify at minimum:

- 320×170 Chinese UI and five-app menu/input.
- boot Stock; no auto-idle switching.
- Weather current + 今/明, no 后天, no dividers, Bad Apple 168×126 / ~10 FPS / loop / exit-reenter behavior.
- HA regression through existing server with no secret leak.
- Bambu config through local :8081 page without secret echo.
- successful non-2FA Cloud login, printer discovery/selection and Cloud MQTT state.
- remote printer data works without printer-LAN reachability when Internet remains available.
- Bambu progress/ETA/layers/temps/job/filament update without UI freeze.
- leaving Bambu does not stop MQTT; returning shows fresh cached state.
- Stock/Weather/HA remain usable while MQTT stays connected.
- Wi-Fi loss/recovery reconnects without watchdog/panic/reboot or monotonic heap loss.
- token invalid -> saved-password relogin/reconnect when safely testable; 2FA behavior is explicit when applicable.
- if practical, changing Bambu configuration while a background Cloud operation is in flight must leave the newer Portal configuration authoritative; no late background result may revert it.
- >=100 app/menu transitions for formal full acceptance.

See `docs/hardware-acceptance.md`.

## Safety

Scope is this repository and connected T-Display-S3. Do not modify unrelated servers/network/DHCP/Wi-Fi infrastructure. Never hard-code passwords, GitHub secrets, HA tokens or Bambu credentials.

When lifecycle/input/config/provider/transport/build/deployment/UI changes, keep README.md, AGENTS.md, docs/deployment.md, docs/api-contract.md and docs/hardware-acceptance.md aligned in the same PR.