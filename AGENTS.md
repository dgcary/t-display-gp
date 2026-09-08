# AGENTS.md — T-Display GP

GitHub `dgcary/t-display-gp` is the source of truth.

## Target / workflow

- LILYGO T-Display-S3 / ESP32-S3 only; ST7789 logical 320×170 landscape rotation 3.
- Arduino/C++17, PlatformIO `lilygo-t-display-s3`.
- ESP32 platform pinned `espressif32@6.12.0` / Arduino-ESP32 2.0.17.
- Web ChatGPT owns source/design/TDD/GitHub/CI/exact-SHA artifact; Codex flashes approved image and performs physical tests.
- Normal deployment: exact artifact application image only; preserve NVS/bootloader/partitions.

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

## Input / shell

```text
normal app: GPIO0 short prev; GPIO14 short next; GPIO0 long menu; GPIO14 long no-op
menu:       GPIO0 short prev; GPIO14 short next; GPIO0 long no-op; GPIO14 long enter
```

Menu exactly `股票 / 天气 / Bambu Lab / 智能家居 / 设备信息`; startup Stock; no automatic idle switching.

## Other apps

Stock: Tencent primary / EastMoney fallback, dedicated `MarketDataWorker`, cache-preserving.

Weather: Open-Meteo current + Today/Tomorrow. Bad Apple x=152,y=27 168×126, 2190 frames, 10 FPS, local flash, silent loop.

HA: read-only REST, 1–4 entities, refresh 30–300 s; HTTP trusted LAN only, HTTPS requires configured CA; no credentialed `setInsecure()`.

## Bambu Lab Cloud

Read-only persistent Cloud integration. No printer-LAN MQTT dependency and no pause/resume/stop/light/temperature/camera control. Only permitted publish: read-only `pushall`.

### Manual Token / config

No Bambu account/password/SMS/email/TFA login flow, password storage, automatic relogin or token renewal. User pastes browser `token` only into trusted-LAN `http://<device-ip>:8081/`. Never hard-code/log/return Token.

Schema v2: `enabled, region, accessToken, cloudUserId, printers[4]{serial,name}, printerCount, activePrinterIndex`. Max 4 config slots, safe unique serials. Physical concurrent-slot acceptance is currently required for the user's real 2-printer setup; do not claim 4 simultaneous sockets hardware-validated until tested.

Device switching:

```text
GPIO0 short  -> previous saved printer
GPIO14 short -> next saved printer
```

Selection wraps and persists. **Switching active printer is local selection only: do not disconnect persistent MQTT slots, do not clear sibling state caches, do not bump a connection-affecting revision.** `snapshot()` / `status()` expose the selected slot cache/status. With fewer than 2 printers, switch is no-op.

Rendering stays partial; whole-screen fill only explicit full redraw/first entry. Routine polling must not flash the screen.

### Portal

Exactly one `WebServer{8081}`: HA status/config plus Bambu status/printers/discover/config/logout. Old Bambu login/verify/resend routes stay retired. Token input password-type, never echoed; blank preserves Token. Discovery explicit-only; `/api/bambu/printers` local-only.

### MQTT — persistent multi-printer BambuHelper alignment

Reference `Keralots/BambuHelper` MIT, pinned migration reference `d7a898394c046495798d87e50afd91ecf63f6ce7`.

```text
CHINA -> cn.mqtt.bambulab.com:8883
US_EU -> us.mqtt.bambulab.com:8883
username = cloudUserId
password = accessToken
per slot subscribe = device/<serial>/report
per slot request   = device/<serial>/request
```

Runtime contract:

- `begin()` initialization only; no custom MQTT task.
- `process(nowMs)` called from Arduino loop; no CPU0-pinned `bambu-mqtt` task.
- `std::array<MqttConn,4>` + `std::array<BambuState,4>` provide independent per-printer connection/runtime/cache.
- service all connected slots first; at most one blocking new/reconnect attempt per loop pass.
- each slot owns its own fresh-on-reconnect `WiFiClientSecure + PubSubClient`; strict CA, `setTimeout(15)`, buffer 40960, keepalive 30, random `bblp_*` client ID.
- PubSubClient owns TCP/TLS establishment; no normal-path preflight or explicit `tls_->connect(...)`.
- callback routes report topic by Serial to the correct slot.
- initial per-slot `pushall` >=2000 ms after connect.
- per-slot backoff 30 s, 60 s after 5 failures, 120 s after 15.
- one slot failure/reconnect must not tear down sibling online slots.
- active index/name-only config changes preserve sockets and slot caches; credential/region/serial-set changes rebuild affected runtime via config revision.
- rc 4/5 latches token-invalid protection.
- `esp_task_wdt_reset()` allowed around known long operations, but watchdog disable/delete forbidden.
- `setInsecure()` forbidden; no background login/discovery/token renewal.
- old Stage-2/3/4 `mqtt_real_tls_*`, layered preflight and project `MQTT_SOCKET_TIMEOUT` overrides stay retired.

Secret-safe serial markers include `slot=<n>` with mqtt_connect/ok/fail/subscribe_fail/loop_lost/pushall. Never print Token, Cloud User ID, Cookie/Authorization/auth payload.

## Diagnostics / acceptance

Physical acceptance must include: exact firmware/NVS preservation; 2 saved real printers both reach their own `mqtt_connect_ok slot=0/1` and `mqtt_pushall_initial`; then >=10 A↔B switches should create **no new mqtt_connect solely because of switching** and should feel near-immediate; per-slot live data/caches must not cross-contaminate; selected active index persists reboot; no periodic full-screen flash; >=10 min no watchdog/panic/automatic reboot; one slot reconnect must not drop the other; check Stock/Weather/HA coexistence and heap under 2 persistent MQTT/TLS slots.
