# T-Display-S3 Hardware Acceptance

Only for real LILYGO T-Display-S3. CI does not replace physical acceptance.

Record source SHA, Actions run, artifact ID, firmware SHA256, date and secret-safe serial evidence.

## Flash

Verify exact-SHA manifest and firmware SHA256. Normal upgrade: flash only `firmware.bin` at manifest offset (normally `0x10000`), preserve NVS, bootloader and partition table. Serial 115200.

## Core UI

- 320×170 landscape, Chinese readable.
- Menu exactly 股票 / 天气 / Bambu Lab / 智能家居 / 设备信息.
- Reboot enters Stock; no auto-idle switching.
- Button debounce/long-press behavior unchanged.

## Weather / Bad Apple

- current + 今/明 only;
- no top/video divider lines;
- Bad Apple 168×126 at x=152,y=27, ~10 FPS, 2190-frame loop;
- leave Weather stops redraw, re-enter starts frame 0;
- no watchdog/panic/freeze/monotonic heap leak.

## Home Assistant

Read-only regression: HTTP trusted-LAN when used; HTTPS requires correct CA and no insecure fallback; 1–4 entities; no Token/Authorization in status/serial.

## Bambu — Manual Token security

User obtains Token from their own browser and pastes it directly into `http://<device-ip>:8081/`. Never send it to ChatGPT/Codex or include it in screenshots/logs.

Verify Token is not echoed; status exposes only `token_set` + non-secret runtime metadata; local printer API exposes names/serials only; logout clears Token/User ID/printers and disables Bambu.

## Bambu — multi-printer / device switching

With at least two saved printers A/B:

1. Select A and Save; MQTT should connect without reboot/account login/verification code.
2. Confirm A identity/live state.
3. GPIO14 short: A→B, old A state clears immediately, no reboot/Token prompt/discovery, B reconnects.
4. GPIO0 short: B→A with wrap.
5. Web selector remains equivalent and reboot-free.
6. Reboot without erase; last active selection persists.
7. With one printer, both short-switch actions are no-op.
8. GPIO0 long still returns menu; GPIO14 long remains no-op.

## Bambu — anti-flicker

Observe Bambu >=2 minutes idle and during live updates when available. No periodic whole-screen blank/black flash at the 500 ms cache polling cadence. First entry and explicit printer switch may full redraw; routine fields only redraw affected sections.

## Explicit discovery

“用 Token 获取我的打印机” runs only after explicit click; results populate browser form and do not persist before Save; failure must not erase local printers; no secret in response/log.

## Reboot persistence / token invalid

After valid config reboot without erase: Token remains set, local list/active selection remain, MQTT reconnects without password/SMS/TFA.

If safely observing MQTT rc 4/5: session becomes `token_invalid`, same rejected config stops retrying, and a newly saved browser Token/config revision resumes connection. Do not intentionally trigger account lockout.

## MQTT stability — BambuHelper-aligned runtime

Candidate must be built with `espressif32@6.12.0` / Arduino-ESP32 2.0.17 and use the loop-driven Cloud MQTT lifecycle adapted from `Keralots/BambuHelper`.

Required architecture evidence:

```text
no dedicated CPU0 bambu-mqtt task
BambuMqttService::process(nowMs) driven from Arduino loop
fresh WiFiClientSecure + PubSubClient on every reconnect
strict CA, TLS Client timeout 15 s
PubSubClient buffer 40960, keepalive 30 s
random bblp_* client ID
direct mqtt.connect(user, token)
subscribe report topic
initial pushall >=2 s after connect
30/60/120 s reconnect backoff
```

The old Stage-2/3/4 `mqtt_real_tls_*`, layered per-attempt probe, explicit pre-connect `tls_->connect(...)`, and project `MQTT_SOCKET_TIMEOUT=3/5` are not production acceptance markers.

`esp_task_wdt_reset()` around known long operations is allowed because it matches the working upstream lifecycle. Disabling/deleting watchdogs is forbidden.

### Mandatory 10-minute test

Observe at least 10 continuous minutes after boot/config restore:

- watchdog = 0;
- panic = 0;
- automatic reboot = 0;
- MQTT connects and remains usable when broker accepts the session;
- live report data continues updating;
- no repeated fixed-cadence reconnect churn;
- a transient connect failure returns normally and later retries under backoff;
- no secret leak.

Expected secret-safe markers:

```text
[bambu] mqtt_connect ... fails=<n>
[bambu] mqtt_connect_ok elapsed_ms=<ms>
[bambu] mqtt_connect_fail rc=<n> elapsed_ms=<ms> retry_ms=<ms>
[bambu] mqtt_subscribe_fail ...
[bambu] mqtt_loop_lost ...
[bambu] mqtt_pushall_initial seq=<n> delay_ms=<ms>
```

### A/B switching under live MQTT

After a stable A session, switch A→B→A at least three cycles. Each switch must clear old state, reconnect to selected serial, require no reauth/reboot, and preserve selection in NVS. No watchdog/panic/state cross-contamination.

## Live state

During a print, verify available fields: friendly name/state, progress, ETA, layers, nozzle/bed/chamber temperatures, job name, filament/AMS. Missing fields may show placeholders; partial reports preserve prior valid fields.

## Background freshness / coexistence

Confirm Bambu online, then visit Stock/Weather/HA/DeviceInfo while state changes. Return to fresh Bambu cache. Stock/Weather/HA network requests must remain usable while persistent MQTT is connected.

## Wi-Fi recovery

Interrupt only T-Display Wi-Fi safely, restore it, and verify same active printer/config, ordinary network loss preserves last valid state, MQTT reconnects, and there is no panic/watchdog/reboot/freeze/monotonic heap leak.

## Final report template

```text
SOURCE SHA:
ACTIONS RUN:
ARTIFACT ID:
FIRMWARE SHA256:
FLASH: PASS/FAIL
NVS ERASED: NO
STOCK START / FIVE APP / NO IDLE: PASS/FAIL
WEATHER / BAD APPLE: PASS/FAIL
HA REGRESSION: PASS/FAIL/NOT TESTED
BAMBU MANUAL TOKEN SAVE: PASS/FAIL
BAMBU SECRET LEAK: PASS/FAIL
BAMBU MQTT 10 MIN: PASS/FAIL
MQTT CONNECT OK COUNT:
MQTT CONNECT FAIL COUNT:
MQTT LOOP LOST COUNT:
RECONNECT BACKOFF OBSERVED:
BAMBU LIVE DATA: PASS/FAIL/PARTIAL
BAMBU DEVICE A->B->A: PASS/FAIL
BAMBU SELECTION PERSISTENCE: PASS/FAIL
BAMBU OLD-STATE CROSS-CONTAMINATION: YES/NO
BAMBU WHOLE-SCREEN FLICKER: PASS/FAIL
BAMBU BACKGROUND COEXISTENCE: PASS/FAIL/NOT TESTED
BAMBU WIFI RECOVERY: PASS/FAIL/NOT TESTED
WATCHDOG COUNT:
PANIC COUNT:
AUTOMATIC REBOOT COUNT:
HEAP OBSERVATION:
PHYSICAL ACCEPTANCE: PASS/FAIL/PARTIAL
RAW SECRET-SAFE LOG:
```
