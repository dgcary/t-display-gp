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
- Button semantics/debounce/long-press unchanged.

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
- active printer remains;
- MQTT reconnects without account password/SMS/TFA flow.

## Token invalid

Only test safely. If MQTT returns auth rc 4/5:

- session becomes `token_invalid`;
- same rejected Token is not repeatedly retried;
- no password login/SMS/email/TFA request occurs;
- obtain a fresh browser Token, paste/save it, and confirm new config revision resumes MQTT.

Do not intentionally trigger account lockout or repeatedly use bad Tokens.

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
BAMBU ACTIVE A -> B SWITCH: PASS/FAIL
BAMBU B -> A SWITCH: PASS/FAIL
BAMBU SWITCH WITHOUT REBOOT: PASS/FAIL
BAMBU OLD-STATE CROSS-CONTAMINATION: PASS/FAIL
BAMBU EXPLICIT DISCOVERY: PASS/FAIL/NOT TESTED
BAMBU MQTT: PASS/FAIL
BAMBU TOKEN REBOOT REUSE: PASS/FAIL
BAMBU TOKEN INVALID BEHAVIOR: PASS/FAIL/NOT TESTED
BAMBU LIVE FIELDS: PASS/FAIL/PARTIAL/NOT TESTED
BAMBU BACKGROUND FRESHNESS: PASS/FAIL/NOT TESTED
BAMBU WIFI RECOVERY: PASS/FAIL/NOT TESTED
WATCHDOG/PANIC/REBOOT:
HEAP OBSERVATION:
PHYSICAL ACCEPTANCE: PASS/FAIL/PARTIAL
```
