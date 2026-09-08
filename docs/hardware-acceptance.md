# T-Display-S3 Hardware Acceptance

Only for real LILYGO T-Display-S3. CI does not replace physical acceptance.

Record source SHA, Actions run, artifact ID, firmware SHA256, date, Wi-Fi and serial evidence.

## Flash

Verify exact-SHA manifest and firmware SHA256. Normal upgrade: flash only `firmware.bin` at manifest offset (normally `0x10000`), preserve NVS, bootloader and partition table. Serial 115200.

## Core UI

- 320×170 landscape, Chinese readable.
- Menu exactly 股票 / 天气 / Bambu Lab / 智能家居 / 设备信息.
- Reboot enters Stock.
- No auto-idle switching.
- Button debounce/long-press behavior unchanged.

## Weather / Bad Apple

- current + 今/明 only, no 后天;
- no top horizontal divider, no weather/video vertical divider;
- Bad Apple 168×126 at x=152,y=27, about 10 FPS, 2190-frame loop;
- leaving Weather stops video redraw, re-enter starts frame 0;
- no watchdog/panic/freeze/monotonic heap leak.

## Home Assistant

- existing HA remains read-only server target;
- HTTP trusted-LAN mode works when used;
- HTTPS requires correct CA and has no insecure fallback;
- 1–4 entities, last-valid cache;
- status/serial do not expose Token/Authorization.

## Bambu — Manual Token security

The user obtains Token from their own browser and pastes it directly into `http://<device-ip>:8081/`. Never send it to ChatGPT/Codex or include it in screenshots/logs.

Verify:

- Token input is not echoed after save;
- `/api/bambu/status` exposes only `token_set` and non-secret runtime metadata;
- `/api/bambu/printers` contains only saved printer names/serials;
- serial logs do not contain Token/User ID/Cookie/Authorization;
- logout clears Token/User ID/local printers and disables Bambu.

## Bambu — multi-printer setup

Use at least two real printers when available.

1. Choose correct region.
2. Paste valid browser Token locally.
3. Configure printer A and B (`name + Serial`) in separate slots, or explicitly use “用 Token 获取我的打印机” once to populate the form.
4. Select A as 当前打印机 and click “保存并切换”.
5. Confirm config persists and MQTT connects without reboot/account login/verification code.
6. Confirm Bambu screen identifies A and receives A state.
7. Return to `:8081`, select B and Save.
8. Confirm device does **not** reboot and does not request Token again.
9. Immediately after switching, old A state must not remain rendered as B; temporary placeholders/connecting are acceptable.
10. Confirm MQTT reconnects/subscribes to B and B state appears.
11. Switch back to A and repeat.

Expected broker/topic:

```text
China  cn.mqtt.bambulab.com:8883
Global us.mqtt.bambulab.com:8883
subscribe device/<activeSerial>/report
```

Only read-only `pushall` request publish is allowed.

## Bambu — device-side switching

With at least two printers already persisted:

1. Enter Bambu with printer A active and online.
2. Short-press GPIO14. The selected printer must advance to B (or next saved printer) and wrap at the end.
3. Old A state must clear immediately; temporary `CONNECTING`/placeholder values are acceptable.
4. No reboot, account login, Token prompt or Cloud discovery request is allowed.
5. MQTT must reconnect/subscribe using B's Serial and B live state must appear.
6. Short-press GPIO0. Selection must move to the previous saved printer and wrap back to A.
7. Reboot without erasing NVS and confirm the last device-side selection persists.
8. Repeat with only one saved printer if convenient: GPIO0/GPIO14 short presses must be no-op.
9. GPIO0 long must still return to menu; GPIO14 long must remain no-op.

## Bambu — anti-flicker rendering

Observe Bambu for at least 2 minutes while idle and during live print updates when available.

- There must be no periodic whole-screen black/blank flash at the 500 ms service polling cadence.
- Entering Bambu and explicitly changing active printer may perform one full redraw.
- Routine state changes may redraw the affected text/progress section but must not `fillScreen()` the entire panel.
- If visible flicker remains, record whether it is whole-screen or confined to a single changing section and provide video if possible.

## Explicit discovery

When testing “用 Token 获取我的打印机”:

- it runs only after explicit click;
- it may use typed Token or existing stored Token;
- returned list populates browser form;
- cancel/reload before Save must leave persisted local printer list unchanged;
- Cloud HTTPS failure must not erase saved local printers;
- no secret in response/log.

## Reboot persistence

After successful Manual Token config, reboot without erasing NVS:

- Token remains set;
- local printer list remains;
- active printer remains, including a device-side button selection;
- MQTT reconnects without account password/SMS/TFA flow.

## Token invalid

Only test safely. If MQTT returns auth rc 4/5:

- session becomes `token_invalid`;
- same rejected Token is not repeatedly retried;
- no password login/SMS/email/TFA request occurs;
- obtain a fresh browser Token, paste/save it, and confirm new config revision resumes MQTT.

Do not intentionally trigger account lockout or repeatedly use bad Tokens.

## MQTT stability — single real TLS acceptance

Previous physical evidence has already established that DNS, plain TCP 8883 and a strict-CA TLS handshake can succeed and that internal contiguous heap is not exhausted. It also established that shortening only PubSubClient's CONNACK timeout to 5 s did not stop the watchdog, so the blocked boundary was the real implicit TLS connection before CONNACK.

The candidate under test must use only one real TLS connection for the normal MQTT path:

```text
[bambu] mqtt_connect ...
[bambu] mqtt_diag_heap phase=before_real_tls ...
[bambu] mqtt_real_tls_begin broker=<...> timeout_ms=5000 ...
[bambu] mqtt_real_tls_ok elapsed_ms=<...> ...
# or bounded mqtt_real_tls_fail
[bambu] mqtt_diag_heap phase=after_real_tls ...
[bambu] mqtt_diag_heap phase=after_mqtt_buffer ...
[bambu] mqtt_connect_ok elapsed_ms=<...> ...
# or bounded mqtt_connect_fail rc=<...> ...
```

Acceptance requirements:

- no routine `mqtt_diag_dns/tcp/tls` preflight sequence before the real MQTT connection;
- `mqtt_real_tls_begin` must return as `mqtt_real_tls_ok` or `mqtt_real_tls_fail` without a task watchdog reboot;
- TLS connect is strict-CA and bounded at 5000 ms;
- if `mqtt_real_tls_ok` occurs, PubSubClient must reuse that socket rather than opening a second TLS connection;
- MQTT CONNECT/CONNACK wait is bounded by `MQTT_SOCKET_TIMEOUT=5`;
- no `disableCore*WDT`, `disableLoopWDT`, `esp_task_wdt_delete` or `esp_task_wdt_reset` workaround;
- no Token/User ID/Cookie/Authorization/account data in serial;
- no raw auth payloads or credential-bearing TLS error text;
- keepalive remains 30 s, reconnect cadence remains 30 s, receive buffer remains 40960 bytes, only read-only pushall is published.

Interpretation:

```text
mqtt_real_tls_fail within <=~5 s
  -> transport/TLS layer failure is bounded and no longer allowed to hang CPU0

mqtt_real_tls_ok + mqtt_connect_fail rc=-4 near <=~5 s
  -> TLS completed; broker did not return CONNACK within PubSub timeout

mqtt_real_tls_ok + mqtt_connect_fail rc=4/5
  -> MQTT authentication/Token rejection

mqtt_connect_ok
  -> continue long-run stability and live-state acceptance

watchdog before either bounded return
  -> FAIL; capture full log and do not mask it by feeding/disabling watchdog
```

At the same time, when topology permits, sample `/api/bambu/status` every ~2 s and record `session`, `mqtt_connected`, `last_mqtt_rc`, active printer and timestamps.

## Live state

During a print, verify available fields: friendly name/state, progress, ETA, layers, nozzle/bed/chamber temperatures, job name, filament/AMS. Missing fields may show placeholders. Partial reports must preserve prior valid fields.

## Background freshness / concurrency

1. Confirm Bambu online.
2. Leave Bambu for Stock/Weather/HA/DeviceInfo while print state changes.
3. Return and confirm fresh cached state.
4. MQTT must not be tied to active app.
5. Stock/Weather/HA network requests remain responsive while MQTT stays connected.

## Wi-Fi recovery

Interrupt only T-Display Wi-Fi safely, then restore:

- same active printer remains configured;
- last valid state is preserved for ordinary network loss;
- Cloud MQTT reconnects;
- no panic/watchdog/reboot/freeze;
- no monotonic heap leak.

## Stability

Formal full acceptance: representative Stock/Weather/HA/Bambu use plus >=100 menu/app transitions. `[sys]` must only report:

```text
MENU|STOCK|WEATHER|BAMBU|HOME_ASSISTANT|DEVICE_INFO
```

## Final report template

```text
SOURCE SHA:
ACTIONS RUN:
ARTIFACT ID:
FIRMWARE SHA256:
FLASH: PASS/FAIL
STOCK START / FIVE APP / NO IDLE: PASS/FAIL
WEATHER / BAD APPLE: PASS/FAIL
HA REGRESSION: PASS/FAIL/NOT TESTED
BAMBU MANUAL TOKEN SAVE: PASS/FAIL
BAMBU TOKEN SECRET LEAK: PASS/FAIL
BAMBU LOCAL PRINTER COUNT:
BAMBU WEB A -> B SWITCH: PASS/FAIL
BAMBU DEVICE NEXT A -> B: PASS/FAIL
BAMBU DEVICE PREV B -> A: PASS/FAIL
BAMBU DEVICE SWITCH PERSISTENCE: PASS/FAIL
BAMBU SWITCH WITHOUT REBOOT: PASS/FAIL
BAMBU OLD-STATE CROSS-CONTAMINATION: PASS/FAIL
BAMBU WHOLE-SCREEN FLICKER: PASS/FAIL
BAMBU EXPLICIT DISCOVERY: PASS/FAIL/NOT TESTED
BAMBU MQTT: PASS/FAIL
BAMBU MQTT DROP COUNT / DURATION:
NETCFG:
BAMBU REAL TLS RESULT / ELAPSED / TLS CODE:
BAMBU INTERNAL HEAP FREE/LARGEST:
BAMBU DMA HEAP FREE:
BAMBU REAL MQTT CONNECT ELAPSED/RC:
BAMBU MQTT DIAGNOSTIC SUMMARY:
BAMBU TOKEN REBOOT REUSE: PASS/FAIL
BAMBU TOKEN INVALID BEHAVIOR: PASS/FAIL/NOT TESTED
BAMBU LIVE FIELDS: PASS/FAIL/PARTIAL/NOT TESTED
BAMBU BACKGROUND FRESHNESS: PASS/FAIL/NOT TESTED
BAMBU WIFI RECOVERY: PASS/FAIL/NOT TESTED
WATCHDOG/PANIC/REBOOT:
HEAP OBSERVATION:
PHYSICAL ACCEPTANCE: PASS/FAIL/PARTIAL
```
