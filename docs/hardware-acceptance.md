# T-Display-S3 Hardware Acceptance

Only for real LILYGO T-Display-S3. CI does not replace physical acceptance. Record source SHA, Actions run, artifact ID, firmware SHA256 and secret-safe evidence.

## Flash / core UI

Normal upgrade: exact `firmware.bin` at manifest offset (normally `0x10000`), preserve NVS/bootloader/partition table; serial 115200. Verify 320×170 UI, menu `股票 / 天气 / Bambu Lab / 智能家居 / 设备信息`, Stock startup and no auto idle switching.

## Other app regression

Weather/Bad Apple: current + 今/明, 168×126 x=152,y=27, ~10 FPS, no divider regression/watchdog/leak.

HA: read-only, trusted-LAN HTTP or configured-CA HTTPS, no insecure fallback or secret echo.

## Bambu security

User pastes their browser Token only into local `http://<device-ip>:8081/`; never expose it to ChatGPT/Codex/screenshots/logs. Verify status contains only token-set/non-secret runtime metadata; logout clears credentials/printers.

## Persistent multi-printer acceptance

With two saved real printers A/B:

1. Boot and wait until **both** slots independently reach `mqtt_connect_ok slot=0` and `slot=1` (order may vary) and receive their delayed `mqtt_pushall_initial`/report data.
2. Confirm A and B caches correspond to the correct Serial/name and live state.
3. Perform GPIO14/GPIO0 A↔B switching at least 10 times.
4. When both slots were already online before a switch, the switch itself must not create a new `[bambu] mqtt_connect`/TLS/MQTT login. It should display the target slot on the next normal UI cadence rather than wait for Cloud reconnection.
5. Switching must not clear or corrupt the sibling cached state. No A data shown under B name or vice versa.
6. Web active selector follows the same rule when only active index/name changes.
7. Leave B active and reboot without erase; B remains selected. Connections are naturally re-established after reboot.
8. With only one configured printer, device prev/next is a no-op.

Code capacity is 4 slots, but this acceptance only proves the user's actual two-printer simultaneous configuration. Do not label 4 simultaneous slots hardware-validated without a separate test.

## Bambu anti-flicker

Observe >=2 min idle/live. No periodic 500 ms whole-screen blank/flash. First entry or presentation full redraw is allowed; routine fields redraw sections only.

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

Observe >=10 continuous minutes with both real printer slots configured:

- watchdog 0, panic 0, automatic reboot 0;
- both online slots continue `mqtt.loop()`/report updates while either one is displayed;
- heap does not monotonically decline under two TLS+MQTT clients;
- no repeated reconnect caused solely by active printer switching;
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

Verify progress/ETA/layers/temps/job/filament as available. Leave Bambu for Stock/Weather/HA/DeviceInfo and return; both Bambu slot caches should continue updating. Stock/Weather/HA must remain usable with two persistent MQTT sockets. This regression is important because multi-slot MQTT increases resource pressure.

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
BAMBU SLOT0 ONLINE/PUSHALL: PASS/FAIL
BAMBU SLOT1 ONLINE/PUSHALL: PASS/FAIL
BAMBU A<->B x10 INSTANT: PASS/FAIL
NEW MQTT CONNECT CAUSED BY ONLINE SWITCH: YES/NO
BAMBU SELECTION PERSISTENCE: PASS/FAIL
STATE CROSS-CONTAMINATION: YES/NO
BAMBU WHOLE-SCREEN FLICKER: PASS/FAIL
BAMBU MQTT 10 MIN: PASS/FAIL
SIBLING SURVIVES SINGLE-SLOT DROP: PASS/FAIL/NOT OBSERVED
STOCK/WEATHER/HA COEXISTENCE: PASS/FAIL/NOT TESTED
WATCHDOG COUNT:
PANIC COUNT:
AUTOMATIC REBOOT COUNT:
HEAP START/MIN/END:
SECRET LEAK: NO
PHYSICAL ACCEPTANCE: PASS/FAIL/PARTIAL
RAW SECRET-SAFE LOG:
```
