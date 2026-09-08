# AGENTS.md — T-Display GP

GitHub `dgcary/t-display-gp` is the source of truth.

## Target

- LILYGO T-Display-S3 / ESP32-S3 only.
- ST7789 physical 170×320, logical 320×170 landscape rotation 3.
- Arduino/C++17, PlatformIO env `lilygo-t-display-s3`.
- Do not silently change target, pins, display or orientation.

## Development / deployment split

Web ChatGPT owns source/design/TDD/implementation/GitHub/CI/ESP32 compile/exact-SHA artifact verification. Codex only flashes the approved prebuilt application image, monitors serial and performs physical tests.

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

Open-Meteo; current + structured 3-day provider data, UI renders current + Today/Tomorrow only. Bad Apple fixed viewport x=152,y=27, 168×126, 2190 frames, 10 FPS, silent, loop, local flash only. No header/vertical dividers, no playback network/task/arbiter traffic.

## Home Assistant

Read-only REST client of the user's existing HA server. 1–4 entities, refresh 30–300 s. HTTP trusted-LAN only; HTTPS requires configured CA. `setInsecure()` forbidden on credentialed HA HTTPS. Weather + HA share exactly one `AppDataWorker`.

## Bambu Lab Cloud

Bambu is a device-level **read-only persistent Cloud MQTT integration**.

No printer-LAN MQTT dependency. No pause/resume/stop/light/temperature/camera control. The only permitted publish is the read-only `pushall` state-sync request.

### Manual Token architecture

The firmware does **not** perform Bambu account/password login, SMS/email verification, TFA, password storage, automatic relogin or automatic token renewal.

The user obtains a Bambu browser `token` manually and pastes it into the trusted-LAN Integrations page:

```text
http://<device-ip>:8081/
```

Never hard-code/log/return Access Token. Token is a full credential.

Bambu config schema v2 persists:

```text
enabled
region
accessToken
cloudUserId
printers[4] { serial, name }
printerCount
activePrinterIndex
```

Legacy schema v1 may be decoded only for migration of reusable `access_token`, `cloud_user_id`, `printer_serial`, `printer_name`; legacy account/password are dropped. Store migrates successful v1 decode back to v2.

Validation rules:

- max 4 printers;
- Serial required for each configured entry, bounded and restricted to safe alnum/`_`/`-` characters;
- no duplicate Serial;
- active index must refer to a configured printer;
- enabled config requires Token, Cloud User ID and at least one printer.

`activeBambuPrinter(config)` is the only runtime selection helper. `selectRelativeBambuPrinter(config, direction)` is the pure wrap-around selection helper used by device-side switching.

### Device-side switching / rendering

Within the Bambu normal app:

```text
GPIO0 short  -> previous saved printer
GPIO14 short -> next saved printer
```

Selection wraps through the local printer list and is a no-op with fewer than two printers. A successful switch persists the new `activePrinterIndex`, increments config revision, clears old `BambuState`, disconnects the old session and reconnects to the new Serial. GPIO0 long still returns to menu; GPIO14 long remains no-op through `AppManager`.

`BambuApp` may poll service snapshots on a fixed cadence, but it must mark dirty only when presentation state changes. `BambuScreen` uses a render signature and section-level redraws. Whole-screen `fillScreen()` is allowed only on a full redraw such as first enter or active-printer switch; routine live updates must clear/redraw only the affected section so the display does not flash periodically.

### Portal

Exactly one `WebServer{8081}` owner: `IntegrationConfigPortal`.

HA routes remain:

```text
/api/ha/status
/api/ha/config
```

Bambu routes:

```text
GET  /api/bambu/status
GET  /api/bambu/printers
POST /api/bambu/discover
POST /api/bambu/config
POST /api/bambu/logout
```

Obsolete routes `/api/bambu/login`, `/api/bambu/verify`, `/api/bambu/verification/resend` must not return as supported flows.

Portal behavior:

- Token input is password-type and never echoed; blank preserves current Token.
- 4 local printer slots: name + Serial.
- active printer selector uses those local slots.
- “保存并切换” persists config and applies immediately; no reboot required for Bambu switch.
- “用 Token 获取我的打印机” is explicit user action only. It may use the newly typed Token or existing stored Token, performs HTTPS discovery, returns a bounded printer list to the browser, and does **not** persist the result until Save.
- `/api/bambu/printers` returns saved local printers only; it must not silently hit Cloud.
- status exposes `token_set`, printer count, active serial/name, MQTT/session/rc only; no Token/User ID.
- logout clears Token/User ID/local printers and disables Bambu.

### Cloud client

`BambuCloudClient` is limited to Token-based operations:

```text
fetchUserId(token, region)
fetchPrinters(token, region)
```

User ID first attempts local JWT parsing; profile HTTPS may be a fallback. Printer discovery is explicit only. HTTPS uses strict CA + `NetworkArbiter`; `setInsecure()` forbidden. No login/SMS/email/TFA endpoints in `BambuCloudClient`.

### MQTT

```text
CHINA -> cn.mqtt.bambulab.com:8883
US_EU -> us.mqtt.bambulab.com:8883
username = cloudUserId
password = accessToken
subscribe = device/<activeSerial>/report
request   = device/<activeSerial>/request
```

- one service task owns connect/subscribe/callback/mqtt.loop();
- receive buffer = 40960 bytes;
- strict CA; `setInsecure()` forbidden;
- connect/reconnect handshake uses `NetworkArbiter`, persistent socket releases it after connect;
- normal MQTT path explicitly establishes one strict-CA `WiFiClientSecure` socket with a 5000 ms connect bound before constructing/connecting PubSubClient;
- PubSubClient must reuse that already-connected Client and must not implicitly create a second TLS socket;
- `MQTT_SOCKET_TIMEOUT=5` bounds PubSubClient CONNACK/socket wait;
- no watchdog disable/delete/reset workaround is permitted;
- app transitions never disconnect MQTT;
- config replacement increments `externalConfigRevision_`, disconnects old session and reconnects from new snapshot;
- changing active printer clears old `BambuState` before reconnect, so printer data cannot cross-contaminate;
- MQTT rc 4/5 sets `TOKEN_INVALID` and suppresses retries with the rejected Token until config revision changes;
- other network failures may retry on bounded reconnect cadence;
- no background Cloud login/discovery/token renewal.

### MQTT TLS/watchdog evidence

Physical tests established the following sequence before the single-real-TLS change:

```text
DNS success
plain TCP 8883 success
strict-CA TLS preflight success
internal_largest ~180 KB after 40960-byte MQTT buffer
real PubSubClient call blocks
CPU0 bambu-mqtt starves IDLE and watchdog reboots
```

Changing only PubSubClient `MQTT_SOCKET_TIMEOUT` from the default ~15 s to 5 s did **not** make the call return with rc=-4; watchdog still fired around 16–25 s. Therefore the observed block is below the CONNACK wait, in the real `WiFiClientSecure::connect()` path.

The normal path must therefore be:

```text
WiFiClientSecure allocation
 -> strict CA
 -> explicit tls_->connect(broker, 8883, 5000)
 -> PubSubClient constructed on the connected Client
 -> 40960-byte MQTT buffer
 -> MQTT CONNECT / CONNACK wait bounded to 5 s
```

Do not call `runLayeredConnectionProbe(broker)` in the normal reconnect path. The old layered probe helper may remain available for a future targeted diagnostic build, but the extra TLS preflight must not precede every real MQTT connection.

Approved serial markers include:

```text
[netcfg] ip mask gateway dns1 dns2 bssid ch wifi
[bambu] mqtt_diag_heap phase=<before_real_tls|after_real_tls|after_mqtt_buffer> internal_free internal_largest dma_free heap_free
[bambu] mqtt_real_tls_begin broker timeout_ms heap
[bambu] mqtt_real_tls_ok elapsed_ms rssi heap
[bambu] mqtt_real_tls_fail tls elapsed_ms wifi rssi heap internal_free internal_largest dma_free
[bambu] mqtt_connect / mqtt_connect_ok / mqtt_connect_fail
[bambu] mqtt_subscribe_fail / mqtt_loop_lost
```

Never print Token, Cloud User ID, Authorization/Cookie values, request payload credentials, raw account data or raw credential-bearing TLS error text. Numeric TLS/socket result codes, broker hostname/resolved IP, elapsed time and heap metrics are allowed.

`BambuApp`/`BambuScreen` are passive readers/renderers except for invoking `cycleActivePrinter()` on the two short-button events.

## Config / security

- AppConfig schema v2 namespace `stockticker`.
- HA separate config blob.
- Bambu separate `bambucloud` blob schema v2.
- No account password, verification code, `tfaKey`, Cookie, Authorization header or raw Token in logs/status.
- Normal firmware upgrade preserves NVS.

## Diagnostics

```text
[md]      Stock
[appdata] WEATHER / HOME_ASSISTANT
[net]     short-lived transport
[netcfg]  startup IP/mask/gateway/DNS/BSSID/channel snapshot
[bambu]   secret-safe MQTT/TLS connection diagnostics only
[sys]     MENU|STOCK|WEATHER|BAMBU|HOME_ASSISTANT|DEVICE_INFO
```

## Physical acceptance

Must verify: five-app UI; Stock startup/no idle switch; Weather/Bad Apple; HA regression; Manual Token setup without echo; at least two local Bambu printers; web and device-side active printer switching without reboot/login; selected active index persists; MQTT reconnects to selected serial; old printer state is not shown after switching; Bambu page has no periodic whole-screen flash; optional explicit discover; Token-invalid behavior; background freshness; Wi-Fi recovery; Stock/Weather/HA coexistence; `mqtt_real_tls_*` and MQTT evidence around any failure; no watchdog/panic/heap leak.
