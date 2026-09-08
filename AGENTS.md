# AGENTS.md — T-Display GP

GitHub `dgcary/t-display-gp` is the source of truth.

## Target

- LILYGO T-Display-S3 / ESP32-S3 only.
- ST7789 physical 170×320, logical 320×170 landscape rotation 3.
- Arduino/C++17, PlatformIO env `lilygo-t-display-s3`.
- ESP32 platform pinned to `espressif32@6.12.0` / Arduino-ESP32 2.0.17.
- Do not silently change target, pins, display or orientation.

## Development / deployment split

Web ChatGPT owns source/design/TDD/implementation/GitHub/CI/ESP32 compile/exact-SHA artifact verification. Codex only flashes approved prebuilt application images, monitors serial and performs physical tests.

Required checks:

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_app_shell_contract.py
python tools/validate_dashboard_apps_contract.py
python tools/validate_bambu_cloud_contract.py
python tools/validate_bambu_pubsub_timeout_contract.py
python tools/validate_bad_apple_contract.py
pio test -e native
python tools/prepare_bad_apple_asset.py
pio run -e lilygo-t-display-s3
```

Normal deployment: exact artifact only; do not erase NVS or rewrite bootloader/partitions.

## Input / app shell

```text
normal app: GPIO0 short prev; GPIO14 short next; GPIO0 long menu; GPIO14 long no-op
menu:       GPIO0 short prev; GPIO14 short next; GPIO0 long no-op; GPIO14 long enter
```

debounce 40 ms, long 700 ms, no hold repeat, long release does not emit short.

Menu order exactly:

```text
股票 / 天气 / Bambu Lab / 智能家居 / 设备信息
```

Startup = STOCK. No automatic idle switching.

## Stock

Tencent quote/intraday primary, EastMoney fallback. Independent health, bounded retries, cache-preserving, dedicated `MarketDataWorker`.

## Weather / Bad Apple

Open-Meteo; current + structured provider data, UI renders current + Today/Tomorrow only. Bad Apple fixed viewport x=152,y=27, 168×126, 2190 frames, 10 FPS, silent, loop, local flash only. No header/vertical dividers, no playback network/task/arbiter traffic.

## Home Assistant

Read-only REST client of the user's existing HA server. 1–4 entities, refresh 30–300 s. HTTP trusted-LAN only; HTTPS requires configured CA. `setInsecure()` forbidden on credentialed HA HTTPS. Weather + HA share exactly one `AppDataWorker`.

## Bambu Lab Cloud

Bambu is a device-level **read-only persistent Cloud MQTT integration**. No printer-LAN MQTT dependency and no pause/resume/stop/light/temperature/camera control. The only permitted publish is the read-only `pushall` state-sync request.

### Manual Token / config

The firmware does not perform Bambu account/password login, SMS/email verification, TFA, password storage, automatic relogin or automatic token renewal. The user obtains a browser `token` and pastes it into trusted-LAN `http://<device-ip>:8081/`.

Never hard-code/log/return Access Token. Token is a full credential.

Schema v2:

```text
enabled
region
accessToken
cloudUserId
printers[4] { serial, name }
printerCount
activePrinterIndex
```

Max 4 printers; safe bounded unique serials; enabled config requires Token + Cloud User ID + >=1 printer. Legacy v1 may migrate reusable Token/User ID/single printer; legacy account/password are dropped.

### Device-side switching / rendering

Within Bambu:

```text
GPIO0 short  -> previous saved printer
GPIO14 short -> next saved printer
```

Selection wraps, persists `activePrinterIndex`, clears old `BambuState`, and reconnects to the selected serial without reboot/re-authentication. Fewer than two printers is a no-op.

`BambuApp` polls snapshots but marks dirty only on presentation changes. `BambuScreen` uses section-level redraws; whole-screen `fillScreen()` is reserved for explicit full redraws such as first entry or printer switch.

### Portal

Exactly one `WebServer{8081}` owner: `IntegrationConfigPortal`.

```text
GET  /api/ha/status
POST /api/ha/config
GET  /api/bambu/status
GET  /api/bambu/printers
POST /api/bambu/discover
POST /api/bambu/config
POST /api/bambu/logout
```

Obsolete `/api/bambu/login`, `/api/bambu/verify`, `/api/bambu/verification/resend` must stay retired. Token input is password-type and never echoed; blank preserves current Token. Discovery is explicit-only and does not persist until Save. `/api/bambu/printers` is local-only. Logout clears Token/User ID/printers and disables Bambu.

### Cloud client

`BambuCloudClient` is Token-only: `fetchUserId(token, region)` and `fetchPrinters(token, region)`. HTTPS uses strict CA + `NetworkArbiter`; `setInsecure()` forbidden. No login/SMS/email/TFA endpoints.

### MQTT — BambuHelper-aligned runtime

Reference: `Keralots/BambuHelper` MIT, pinned during this migration at `d7a898394c046495798d87e50afd91ecf63f6ce7`.

```text
CHINA -> cn.mqtt.bambulab.com:8883
US_EU -> us.mqtt.bambulab.com:8883
username = cloudUserId
password = accessToken
subscribe = device/<activeSerial>/report
request   = device/<activeSerial>/request
```

Runtime contract:

- `BambuMqttService::begin()` initializes state only; it creates no dedicated MQTT task.
- `BambuMqttService::process(nowMs)` is called from Arduino `loop()` and is the single caller for reconnect, `mqtt.loop()` and delayed pushall.
- no `xTaskCreatePinnedToCore(... "bambu-mqtt" ...)`; MQTT is not pinned to CPU0.
- each Cloud reconnect destroys old PubSubClient + WiFiClientSecure and creates fresh objects.
- `WiFiClientSecure`: strict CA bundle, `setTimeout(15)`; no `setInsecure()`.
- `PubSubClient@2.8`: 40960-byte receive buffer, keepalive 30 s.
- PubSubClient owns the TCP/TLS handshake; normal reconnect must not pre-open an explicit TLS socket or run layered TLS preflight.
- Cloud client ID is randomized with `bblp_...`, then direct `mqtt.connect(clientId, cloudUserId, accessToken)`.
- subscribe succeeds before the session is ONLINE.
- initial read-only `pushall` is delayed >=2000 ms after connect.
- reconnect backoff: 30 s initially, 60 s after 5 failures, 120 s after 15 failures.
- rc 4/5 latches `TOKEN_INVALID` until config revision changes.
- `esp_task_wdt_reset()` is permitted around known long MQTT/callback/publish operations, matching upstream; watchdogs must never be disabled or deleted.
- `NetworkArbiter` covers the blocking connect/subscribe transaction; the established persistent socket does not hold it.
- app transitions never disconnect MQTT.
- active-printer change clears old state before reconnect.
- no background Cloud login/discovery/token renewal.

The Stage-2/3/4 experimental `mqtt_real_tls_*`, explicit `tls_->connect(...)`, layered reconnect preflight and project `MQTT_SOCKET_TIMEOUT` overrides are retired from production runtime.

Secret-safe serial markers include `[bambu] mqtt_connect`, `mqtt_connect_ok`, `mqtt_connect_fail`, `mqtt_subscribe_fail`, `mqtt_loop_lost`, `mqtt_pushall_initial`. Never print Token, Cloud User ID, Cookie/Authorization or authentication payloads.

## Config / security

- AppConfig schema v2 namespace `stockticker`.
- HA separate config blob.
- Bambu namespace `bambucloud`, schema v2.
- No account password, verification code, `tfaKey`, Cookie, Authorization header or raw Token in logs/status.
- Normal firmware upgrade preserves NVS.

## Diagnostics

```text
[md]      Stock
[appdata] WEATHER / HOME_ASSISTANT
[net]     short-lived transport
[netcfg]  startup IP/mask/gateway/DNS/BSSID/channel snapshot
[bambu]   secret-safe MQTT lifecycle diagnostics
[sys]     MENU|STOCK|WEATHER|BAMBU|HOME_ASSISTANT|DEVICE_INFO
```

## Physical acceptance

Must verify: five-app UI; Stock startup/no idle switch; Weather/Bad Apple; HA regression; Manual Token without echo; at least two local Bambu printers; web/device active-printer switching without reboot/login; selected active index persists; MQTT reconnects to selected serial; no state cross-contamination; no periodic whole-screen Bambu flash; explicit discovery semantics; Token-invalid behavior; background freshness; Wi-Fi recovery; Stock/Weather/HA coexistence; >=10 min Bambu MQTT stability with no watchdog/panic/automatic reboot/secret leak.
