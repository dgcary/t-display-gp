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

Bambu config schema v2:

```text
enabled
region
accessToken
cloudUserId
printers[4] { serial, name }
printerCount
activePrinterIndex
```

Enabled config requires Token, Cloud User ID and >=1 printer. Max 4 printers, no duplicate serials, active index must be valid. Legacy schema v1 decode migrates reusable Token/User ID/single printer and drops account/password.

The firmware has no Bambu account/password login state machine and no SMS/email/TFA verification or automatic token renewal.

`activeBambuPrinter(config)` resolves the selected entry. `selectRelativeBambuPrinter(config, direction)` is a pure selection helper: positive moves to the next configured printer, negative moves to the previous one, both wrap, and fewer than two printers is a no-op.

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

### GET /api/bambu/status

Returns only non-secret state such as:

```text
enabled
region
token_set
printer_count
active_printer_serial
active_printer_name
mqtt_connected
session
last_mqtt_rc
```

No raw Token/Cloud User ID/Cookie/Authorization header.

### GET /api/bambu/printers

Returns the **saved local printer list only**. No implicit Cloud call.

### POST /api/bambu/discover

Explicit user action. Inputs: region + optional newly typed Access Token. Blank Token may use the stored Token. Cloud User ID is resolved locally from JWT when possible, otherwise profile HTTPS fallback may be used. Device-list HTTPS returns a bounded printer list to the browser. Discovery does not persist printers/config.

### POST /api/bambu/config

Inputs:

```text
enabled
region
access_token   # blank preserves stored Token
printer1_name / printer1_serial
...
printer4_name / printer4_serial
active_printer_serial
```

If a new Token is supplied, Cloud User ID is resolved before enabling. Save persists schema v2 and applies immediately through `BambuMqttService::replaceConfig`; no reboot is required for Bambu switching.

### POST /api/bambu/logout

Clears Token/User ID/printer list, disables Bambu and disconnects MQTT.

## Device-side active printer control

While Bambu is the visible normal app:

```text
GPIO0 PREV_SHORT  -> BambuMqttService::cycleActivePrinter(-1)
GPIO14 NEXT_SHORT -> BambuMqttService::cycleActivePrinter(+1)
```

`cycleActivePrinter()` changes only the active index of the existing validated local list, persists the resulting schema-v2 config, increments external config revision, clears old `BambuState` and allows the service task to disconnect/reconnect to the new Serial. No Token/Cloud discovery/login request is involved. Long-press app-shell behavior is unchanged.

## Token HTTPS client

`BambuCloudClient` exposes only Token-based operations:

```text
fetchUserId(token, region)
fetchPrinters(token, region)
```

Bambu HTTPS uses `WiFiClientSecure` CA bundle + bounded response body + shared `NetworkArbiter`. `setInsecure()` is forbidden. Login/SMS/email/TFA endpoints are absent from the client.

## MQTT

```text
CHINA -> cn.mqtt.bambulab.com:8883
US_EU -> us.mqtt.bambulab.com:8883
username  = cloudUserId
password  = accessToken
subscribe = device/<activeSerial>/report
request   = device/<activeSerial>/request
```

One service task owns connect/subscribe/callback/mqtt.loop. Buffer = 40960 bytes. Connect handshake acquires `NetworkArbiter`; established socket releases it. Only one read-only `pushall` publish path is allowed.

Config replacement or device-side active-printer switching increments an external revision. On revision change service disconnects old MQTT, resets retry/token-rejected state and reconnects from the new config. Active printer switch clears old `BambuState` before reconnect.

MQTT auth rc 4/5 sets `TOKEN_INVALID` and marks current Token rejected; that same config is not retried. A new portal config revision (normally fresh Token) clears the latch and permits connection.

When `mqtt.loop()` reports a lost connection the service records a network-error session and the current PubSubClient rc instead of leaving a stale ONLINE label. Reconnect cadence and keepalive remain explicit policy and must not be silently changed while diagnosing a physical-network problem.

## Layered connection diagnostics

The diagnostic revision augments MQTT connection attempts with a bounded preflight under the same `NetworkArbiter` lock:

```text
DNS resolution
plain TCP :8883
strict-CA TLS :8883
real PubSubClient MQTT CONNECT
```

The preflight is observability only. It must not skip the real MQTT CONNECT or change MQTT authentication. Both TLS preflight and the real MQTT socket use strict CA; no insecure fallback is permitted.

Secret-safe serial records:

```text
[netcfg] ip mask gateway dns1 dns2 bssid ch wifi
[bambu] mqtt_diag_heap phase=<before_probe|after_probe|after_mqtt_buffer> internal_free=<bytes> internal_largest=<bytes> dma_free=<bytes> heap_free=<bytes>
[bambu] mqtt_diag_dns ok=<0|1> ip=<resolved-ip> elapsed_ms=<ms>
[bambu] mqtt_diag_tcp ok=<0|1> ip=<resolved-ip|unresolved> port=8883 elapsed_ms=<ms>
[bambu] mqtt_diag_tls ok=<0|1> tls=<numeric> elapsed_ms=<ms>
[bambu] mqtt_connect_ok elapsed_ms=<ms> ...
[bambu] mqtt_connect_fail rc=<mqtt> tls=<numeric> elapsed_ms=<ms> ... internal_free=<bytes> internal_largest=<bytes> dma_free=<bytes>
```

`internal_free` and `internal_largest` come from heap-capability queries for internal 8-bit memory; `dma_free` is DMA-capable free memory. This is required because total heap alone cannot prove that mbedTLS has a sufficiently large contiguous internal block.

Interpretation contract:

```text
DNS fail                           => resolver/network-config layer
DNS pass, TCP fail                 => socket/route/TCP layer
TCP pass, strict TLS preflight fail=> TLS/CA/handshake/resource layer
TLS preflight pass, real rc=-2     => compare post-MQTT-buffer memory and real-connect elapsed time
TLS pass, MQTT rc 4/5              => authentication/Token layer
```

The plain TCP/TLS preflight intentionally creates additional short-lived connections while diagnostics are enabled. It is not a permanent connection optimization and must be reassessed/removed after the physical root cause is identified. Diagnostic instrumentation does not change 30 s keepalive, 30 s reconnect cadence, CA policy, Token policy or the single read-only publish rule.

All diagnostic lines must exclude Access Token, Cloud User ID, Cookie/Authorization, account data, raw authentication payloads and raw credential-bearing TLS error text. Broker hostname/resolved IP, elapsed time, numeric errors, Wi-Fi metadata and heap metrics are allowed.

Network disconnects preserve last valid state except deliberate active-printer change, where state is cleared to prevent showing one printer's data under another printer's name.

## Bambu presentation

`BambuApp` polls the mutex-protected service state/config cache, but a polling tick marks the app dirty only if presentation-relevant state changed. `BambuScreen` keeps a render signature. Full redraw is reserved for first render/explicit app full redraw (including active-printer switch); routine state deltas repaint only the affected header/progress/job/filament/footer region. This prevents whole-screen flashing at the polling cadence.

# Shared concurrency

```text
Stock -> dedicated MarketDataWorker
Weather + HomeAssistant -> one shared AppDataWorker
Bambu -> one persistent MQTT service/task
DeviceInfo -> local-only
Bad Apple -> local flash playback
```

All ordinary short-lived external HTTP/TLS work serializes through `NetworkArbiter`. Bambu persistent MQTT holds it only for connect/reconnect handshake. The diagnostic DNS/TCP/TLS preflight is also executed inside that connection critical section to avoid overlapping other short-lived TLS handshakes.

# Security

No Bambu/HA Token, password, verification code, Cookie, Authorization header or challenge material in logs/status. Portal itself is trusted-LAN HTTP; user must treat pasted Tokens as exposed to that LAN segment.