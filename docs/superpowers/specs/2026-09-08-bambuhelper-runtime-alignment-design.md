# BambuHelper-Aligned MQTT Runtime Design

## Context

Physical testing on `feature/bambu-cloud` established that DNS, TCP/8883, strict-CA TLS and Bambu Cloud authentication can all succeed, but the existing dedicated `bambu-mqtt` task pinned to CPU0 can starve IDLE0 while PubSubClient waits for CONNACK. Stage-4 proved the boundary by shortening `MQTT_SOCKET_TIMEOUT` to 3 seconds: a TLS-success attempt returned `rc=-4` at ~3004 ms without watchdog, while a separate attempt connected successfully in 67 ms.

A known-working reference exists at `Keralots/BambuHelper` (MIT), pinned for this migration at commit `d7a898394c046495798d87e50afd91ecf63f6ce7`. A separate T-Display-S3 running upstream BambuHelper has been reported stable in the same usage context.

## Goal

Replace the experimental Bambu MQTT connection/runtime path with a high-fidelity adaptation of BambuHelper's proven Cloud MQTT execution model while preserving this project's Manual Token setup, active-printer selection, read-only state model, anti-flicker UI and shared network arbitration.

## Reference behavior to adopt

For Cloud MQTT, BambuHelper uses:

- PlatformIO `espressif32@6.12.0` / Arduino-ESP32 2.0.17.
- `WiFiClientSecure` with the built-in CA bundle and `setTimeout(15)`.
- `PubSubClient@2.8`, 40960-byte receive buffer and Cloud keepalive 30 seconds.
- PubSubClient owns the TCP/TLS connect; there is no separate TLS pre-connect immediately before MQTT.
- Random Cloud client IDs in the form `bblp_<random>` to avoid stale-session collision.
- Cloud MQTT connect as `mqtt.connect(clientId, cloudUserId, token)`.
- Subscribe to `device/<serial>/report` after CONNECT.
- Initial read-only `pushall` after a 2-second post-connect delay, not immediately inside the CONNECT transaction.
- Complete TLS/MQTT client destruction before a Cloud reconnect so stale socket/session state cannot survive.
- Cloud reconnect backoff: 30 s initially, 60 s after 5 consecutive failures, 120 s after 15 consecutive failures.
- MQTT service work driven from the Arduino main loop instead of a custom task pinned to CPU0.
- Task-watchdog reset calls around known long MQTT/TLS/callback operations, matching the reference behavior; watchdogs must never be disabled or deleted.

## T-Display GP adaptation

### Execution model

`BambuMqttService::begin()` becomes initialization-only. It no longer creates `bambu-mqtt` with `xTaskCreatePinnedToCore`.

A new public `BambuMqttService::process(uint32_t nowMs)` is called from `loop()` once per iteration after portal processing and before app input/tick/render. This function owns reconnect decisions, `mqtt.loop()`, delayed initial `pushall`, config-revision application and connection-state transitions.

All PubSubClient operations therefore occur on the same Arduino loop context, matching BambuHelper's single-caller execution model. Existing mutex-protected snapshot/config APIs may remain to minimize unrelated refactoring, but no dedicated MQTT task exists.

### Connection path

Normal Cloud reconnect is:

```text
release old PubSubClient/WiFiClientSecure
 -> NetworkArbiter lock
 -> allocate WiFiClientSecure
 -> setCACertBundle(rootca_crt_bundle_start)
 -> setTimeout(15)
 -> allocate PubSubClient over TLS client
 -> setServer(broker, 8883)
 -> setBufferSize(40960)
 -> setCallback(...)
 -> setKeepAlive(30)
 -> random bblp_* client ID
 -> mqtt.connect(clientId, cloudUserId, accessToken)
 -> subscribe device/<activeSerial>/report
 -> release NetworkArbiter
 -> after >=2000 ms, publish the single permitted pushall request
```

The Stage-2/3/4 diagnostic production path is retired:

- no `mqtt_real_tls_begin/ok/fail` normal-path pre-connect;
- no `runLayeredConnectionProbe()` call in normal reconnect;
- no project override of `MQTT_SOCKET_TIMEOUT=3` or `=5`;
- no explicit `tls_->connect(...)` before PubSubClient.

Secret-safe diagnostics may retain connect result, rc, elapsed time, Wi-Fi status/RSSI and heap metrics, but never Token, Cloud User ID or authorization material.

### Backoff and reconnect cleanup

On every unsuccessful Cloud MQTT connect or subscribe:

1. record rc/session state;
2. increment `consecutiveFails_` unless the failure is rc 4/5 token rejection;
3. destroy both PubSubClient and WiFiClientSecure;
4. defer the next attempt using the reference Cloud backoff schedule.

On a stable successful connect, set `connectTimeMs_`, clear/reduce backoff state, arm delayed initial pushall and mark the session online. rc 4/5 continues to latch `TOKEN_INVALID` until a new config revision is applied.

Changing active printer still persists `activePrinterIndex`, clears old `BambuState`, destroys the old MQTT/TLS session and reconnects against the new serial.

### Read-only boundary

Only the existing `pushall` request remains publishable. Do not import BambuHelper light, camera, power, temperature, pause/resume/stop, HMS control or Tasmota command paths.

### Network coexistence

Keep `NetworkArbiter` around the blocking MQTT connect/subscribe transaction so Stock/Weather/HA do not start a second short-lived TLS handshake concurrently. Release the arbiter after the persistent MQTT session is established. `mqtt.loop()` and the established socket do not hold the arbiter.

The main loop may be temporarily delayed by a failed Cloud connect, matching BambuHelper's model. Backoff prevents repeated long blocking attempts from dominating normal UI operation.

## Platform alignment

Change only the ESP32 target platform from `espressif32@6.5.0` to `espressif32@6.12.0`. Keep Arduino framework, C++17, board target, display pins/orientation and current direct dependencies otherwise unchanged.

The native test environment stays `platform = native`.

## Attribution

`THIRD_PARTY_NOTICES.md` already identifies Keralots/BambuHelper as MIT. Update the notice to state that the Cloud MQTT connection lifecycle, reconnect/backoff model and loop-driven scheduling are adapted from the pinned upstream reference. Do not import unrelated upstream UI or device-control features.

## Verification

Static contract must prove:

- ESP32 platform is exactly `espressif32@6.12.0`;
- no `MQTT_SOCKET_TIMEOUT` override remains;
- no `xTaskCreatePinnedToCore`, `taskThunk`, `taskLoop`, `TaskHandle_t task_` or `"bambu-mqtt"` task remains in BambuMqttService;
- `main.cpp` calls `bambuMqttService.process(nowMs)`;
- normal path uses CA bundle, Cloud TLS timeout 15 s, PubSubClient 40960 buffer, keepalive 30, random `bblp_` client ID and direct `mqtt_->connect(...)`;
- no normal-path explicit `tls_->connect(...)` or layered preflight;
- failed reconnect destroys both client objects;
- Cloud backoff constants are 30/60/120 seconds at 5/15-failure thresholds;
- initial pushall is delayed 2000 ms after successful connect;
- watchdog reset calls may exist, but watchdog disable/delete calls are forbidden;
- `setInsecure()` remains forbidden in Bambu Cloud MQTT.

Full acceptance remains exact-head CI plus physical validation: 10+ minutes without watchdog/panic/automatic reboot; MQTT live-data continuity; reconnection after transient failure; A/B printer switching; no state cross-contamination; Stock/Weather/HA regression; anti-flicker rendering; no secret leak.