# T-Display-S3 Hardware Acceptance

Only for real LILYGO T-Display-S3. CI does not replace physical acceptance. Record source SHA, Actions run, artifact ID, firmware SHA256 and secret-safe evidence.

## Flash / direct UI

Normal upgrade: exact `firmware.bin` at manifest offset (normally `0x10000`), preserve NVS/bootloader/partition table; serial 115200.

Verify startup lands directly on **Weather**. Runtime menu must not appear.

Button contract:

```text
GPIO0 short  previous page
GPIO14 short next page
GPIO0 long   no-op
GPIO14 long  no-op
```

Expected forward order, with absent configured slots skipped automatically:

```text
Weather
-> Stock1 -> Stock2 -> Stock3 -> Stock4
-> Bambu1 -> Bambu2
-> Home Assistant
-> Device Info
-> Weather
```

Reverse navigation must traverse the same existing pages in reverse. Direct navigation exposes no more than the first 4 configured stocks and first 2 configured Bambu printers. Example: with only 3 configured stocks and one printer, Stock4 and Bambu2 must not appear.

Configuration remains Web-based: stock/weather on the normal settings portal and HA/Bambu on trusted-LAN port 8081.

## Other app regression

Weather/Bad Apple: current + 今/明, 168×126 x=152,y=27, ~10 FPS, no divider regression/watchdog/leak.

Stock: every exposed Stock page must match the configured order and symbol/name. Moving Stock1->Stock2 etc. must select the requested stock directly without intermediate app exit/re-entry.

HA: read-only, trusted-LAN HTTP or configured-CA HTTPS, no insecure fallback or secret echo.

## Bambu security

User pastes their browser Token only into local `http://<device-ip>:8081/`; never expose it to ChatGPT/Codex/screenshots/logs. Verify status contains only token-set/non-secret runtime metadata; logout clears credentials/printers.

## Persistent multi-printer / direct Bambu page acceptance

With two saved real printers A/B:

1. Boot and wait until **both** slots independently reach `mqtt_connect_ok slot=0` and `slot=1` and receive their delayed `mqtt_pushall_initial`/report data.
2. Confirm A and B caches correspond to the correct Serial/name and live state.
3. Navigate into Bambu1, then Bambu2, then back to Bambu1 at least 10 cycles using the global previous/next page controls.
4. When both slots were already online, moving between Bambu pages must not create a new `[bambu] mqtt_connect`/TLS/MQTT login solely because of navigation. It should display the target slot on the next normal UI cadence rather than wait for Cloud reconnection.
5. Navigation must not clear or corrupt sibling cached state. No A data shown under B name or vice versa.
6. Leave Bambu2 selected, move to another application and back; Bambu2 remains the active printer page unless subsequent global navigation explicitly selects Bambu1.
7. Reboot without erase and verify the persisted Bambu active index remains valid.
8. With only one configured printer, the Bambu2 page is absent from the global chain rather than showing an empty page.

Code capacity is 4 Bambu slots, but direct navigation intentionally exposes only the first two printer pages. Do not label 4 simultaneous slots hardware-validated without a separate test.

## Bambu anti-flicker

Observe >=2 min idle/live. No periodic 500 ms whole-screen blank/flash. First entry or explicit page/full redraw is allowed; routine fields redraw sections only.

## MQTT architecture evidence

Candidate must use `espressif32@6.12.0` / Arduino-ESP32 2.0.17 and BambuHelper-aligned loop-driven persistent slots:

```text
no dedicated CPU0 bambu-mqtt task
BambuMqttService::process(nowMs) from Arduino loop
per-printer MqttConn + BambuState
strict CA, timeout 15 s
PubSubClient buffer 40960, keepalive 30 s
random bblp_* client ID
per-slot subscribe report topic
per-slot initial pushall >=2 s after connect
per-slot 30/60/120 s backoff
```

Old `mqtt_real_tls_*`, layered preflight, explicit production TLS preconnect and project `MQTT_SOCKET_TIMEOUT=3/5` are forbidden acceptance markers. WDT reset around known long operations is allowed; disabling/deleting watchdog is forbidden.

## Mandatory stability

Observe >=10 continuous minutes with the real configured page set and both real printer slots configured:

- watchdog 0, panic 0, automatic reboot 0;
- direct navigation remains responsive and preserves exact ordering/wrap behavior;
- both online Bambu slots continue `mqtt.loop()`/report updates while either one or another app is displayed;
- heap does not monotonically decline under two TLS+MQTT clients;
- no repeated reconnect caused solely by moving between Bambu pages;
- natural failure of one slot, if observed, only reconnects that slot; sibling remains connected/fresh;
- no secret leak.

Expected markers:

```text
[bambu] mqtt_connect slot=<n> ...
[bambu] mqtt_connect_ok slot=<n> elapsed_ms=...
[bambu] mqtt_connect_fail slot=<n> rc=... retry_ms=...
[bambu] mqtt_subscribe_fail slot=<n> ...
[bambu] mqtt_loop_lost slot=<n> ...
[bambu] mqtt_pushall_initial slot=<n> ...
```

## Live/background/coexistence

Verify Bambu progress/ETA/layers/temps/job/filament as available. Leave Bambu for Stock/Weather/HA/DeviceInfo and return; both Bambu slot caches should continue updating. Stock/Weather/HA must remain usable with persistent MQTT sockets.

## Wi-Fi recovery

Safe Wi-Fi interruption/recovery: preserve config/active selection; reconnect slots without panic/watchdog/reboot/freeze/monotonic heap leak. One slot may recover before another.

## Report template

```text
SOURCE SHA:
ACTIONS RUN:
ARTIFACT ID:
FIRMWARE SHA256:
FLASH: PASS/FAIL
NVS ERASED: NO
STARTUP WEATHER: PASS/FAIL
FORWARD PAGE ORDER: PASS/FAIL
REVERSE PAGE ORDER: PASS/FAIL
MISSING SLOT AUTO-SKIP: PASS/FAIL
LONG PRESS NO-OP: PASS/FAIL
STOCK PAGE MAPPING: PASS/FAIL
BAMBU SLOT0 ONLINE/PUSHALL: PASS/FAIL
BAMBU SLOT1 ONLINE/PUSHALL: PASS/FAIL
BAMBU PAGE1<->PAGE2 x10 INSTANT: PASS/FAIL
NEW MQTT CONNECT CAUSED BY ONLINE PAGE SWITCH: YES/NO
BAMBU SELECTION PERSISTENCE: PASS/FAIL
STATE CROSS-CONTAMINATION: YES/NO
BAMBU WHOLE-SCREEN FLICKER: PASS/FAIL
BAMBU MQTT 10 MIN: PASS/FAIL
STOCK/WEATHER/HA COEXISTENCE: PASS/FAIL/NOT TESTED
WATCHDOG COUNT:
PANIC COUNT:
AUTOMATIC REBOOT COUNT:
HEAP START/MIN/END:
SECRET LEAK: NO
PHYSICAL ACCEPTANCE: PASS/FAIL/PARTIAL
RAW SECRET-SAFE LOG:
```