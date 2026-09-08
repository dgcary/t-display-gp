# Data Provider / HTTP API Contract

Status date: 2026-09-08

Remote payloads stay behind provider/service abstractions. UI consumes structured state.

## Provider matrix

| Capability | Provider |
|---|---|
| A-share quote/intraday | Tencent primary, EastMoney fallback |
| Weather | Open-Meteo |
| Home Assistant | user's existing HA REST API |
| Bambu printer | Bambu Cloud Token HTTPS + persistent MQTT |
| Bad Apple | local compiled flash asset |
| Device Info | local device state |

# Home Assistant

Read-only requests:

```text
GET <base_url>/api/states/<entity_id>
Authorization: Bearer <token>
```

HTTP is trusted-LAN cleartext. HTTPS requires configured CA. No `/api/services` writes. 1–4 entities, refresh 30–300 s, last-valid cache.

# Bambu Lab Cloud

## Configuration

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

Enabled config requires Token, Cloud User ID and >=1 printer. Max 4 printers, no duplicate serials, active index valid. Legacy v1 may migrate reusable Token/User ID/single printer and drops account/password. Firmware has no account/password/SMS/email/TFA state machine and no automatic token renewal.

`activeBambuPrinter(config)` resolves the selected entry. `selectRelativeBambuPrinter(config, direction)` performs wrap-around local selection and is a no-op with fewer than two printers.

## Local portal

Exactly one `WebServer{8081}` exists: `IntegrationConfigPortal`.

```text
GET  /api/ha/status
POST /api/ha/config
GET  /api/bambu/status
GET  /api/bambu/printers
POST /api/bambu/discover
POST /api/bambu/config
POST /api/bambu/logout
```

`/api/bambu/status` exposes only non-secret runtime metadata (`token_set`, printer count, active name/serial, MQTT/session/rc). `/api/bambu/printers` returns only saved local printers. Discovery is explicit and does not persist until Save. Blank Access Token on config Save preserves stored Token. Logout clears Token/User ID/printers and disables Bambu.

Old `/api/bambu/login`, `/api/bambu/verify`, `/api/bambu/verification/resend` are unsupported.

## Device-side active printer control

```text
GPIO0 PREV_SHORT  -> BambuMqttService::cycleActivePrinter(-1)
GPIO14 NEXT_SHORT -> BambuMqttService::cycleActivePrinter(+1)
```

Selection changes only local validated config, persists v2, increments config revision, clears old BambuState and reconnects to the new Serial. It does not invoke Cloud discovery/login.

## Token HTTPS client

`BambuCloudClient` exposes only:

```text
fetchUserId(token, region)
fetchPrinters(token, region)
```

Bambu HTTPS uses strict CA + shared `NetworkArbiter`; `setInsecure()` forbidden.

## MQTT protocol

```text
CHINA -> cn.mqtt.bambulab.com:8883
US_EU -> us.mqtt.bambulab.com:8883
username  = cloudUserId
password  = accessToken
subscribe = device/<activeSerial>/report
request   = device/<activeSerial>/request
```

Only the read-only `pushall` request may be published. Receive buffer = 40960 bytes. Keepalive = 30 s.

### Runtime ownership

MQTT is **main-loop driven**, adapted from `Keralots/BambuHelper`'s MIT Cloud connection lifecycle:

```text
setup: BambuMqttService::begin(config, store)
loop:  BambuMqttService::process(nowMs)
```

There is no custom FreeRTOS `bambu-mqtt` task and no CPU0 pinning. `process()` owns reconnect decisions, `mqtt.loop()`, config-revision application and delayed initial pushall.

### Cloud reconnect contract

Every new Cloud attempt starts from fresh clients:

```text
destroy old PubSubClient/WiFiClientSecure
acquire NetworkArbiter
new WiFiClientSecure
setCACertBundle(rootca_crt_bundle_start)
setTimeout(15)
new PubSubClient(*tls)
setServer(broker, 8883)
setBufferSize(40960)
setKeepAlive(30)
random client ID bblp_<random>
mqtt.connect(clientId, cloudUserId, accessToken)
subscribe device/<serial>/report
release NetworkArbiter
>=2000 ms after successful connect -> one pushall
```

PubSubClient owns TCP/TLS connection establishment. Normal reconnect must not first perform a layered diagnostic preflight or explicit `tls_->connect(...)`. Project-level `MQTT_SOCKET_TIMEOUT=3/5` diagnostic overrides are retired.

Reconnect backoff:

```text
0..4 consecutive failures   -> 30000 ms
5..14 consecutive failures  -> 60000 ms
>=15 consecutive failures   -> 120000 ms
```

On connect/subscribe/loop failure, stale MQTT/TLS clients are destroyed. rc 4/5 sets `TOKEN_INVALID` and latches the rejected config until external config revision changes. Successful connect resets failure backoff and arms the 2-second initial pushall.

Changing active printer increments config revision, clears old state and forces session recreation with the selected Serial.

### Watchdog policy

The previous CPU0-pinned service could starve IDLE0 during a PubSubClient CONNACK busy wait. The production runtime fixes execution ownership by moving MQTT work to Arduino loop context and aligning framework/runtime behavior with the known-working reference.

`esp_task_wdt_reset()` is allowed around known long MQTT connect/callback/pushall operations, matching BambuHelper. It is forbidden to disable/delete watchdog protection (`disableCore*WDT`, `disableLoopWDT`, `esp_task_wdt_delete`).

### Diagnostics / security

Secret-safe lines may include:

```text
[bambu] mqtt_connect ...
[bambu] mqtt_connect_ok elapsed_ms=...
[bambu] mqtt_connect_fail rc=... elapsed_ms=... retry_ms=...
[bambu] mqtt_subscribe_fail ...
[bambu] mqtt_loop_lost ...
[bambu] mqtt_pushall_initial ...
```

Never emit Access Token, Cloud User ID, Cookie/Authorization, passwords, verification codes or auth payloads. Bambu Cloud TLS is strict CA; `setInsecure()` forbidden.

## Bambu presentation

`BambuApp` polls mutex-protected service state/config but marks dirty only when presentation changes. `BambuScreen` uses section-level redraws; full `fillScreen()` only on full redraw such as first entry/active-printer switch.

# Shared concurrency

```text
Stock -> dedicated MarketDataWorker
Weather + HomeAssistant -> shared AppDataWorker
Bambu -> loop-driven persistent MQTT service
DeviceInfo -> local-only
```

Short-lived external HTTP/TLS and Bambu connect/reconnect transactions serialize through `NetworkArbiter`. Established MQTT does not hold the arbiter.

# Security

No Bambu/HA Token, password, verification code, Cookie, Authorization header or challenge material in logs/status. Portal itself is trusted-LAN HTTP; pasted Tokens are exposed to that LAN segment.
