# Data Provider / HTTP API Contract

Status date: 2026-09-07

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

Config replacement increments an external revision. On revision change service disconnects old MQTT, resets retry/token-rejected state and reconnects from the new config. Active printer switch clears old `BambuState` before reconnect.

MQTT auth rc 4/5 sets `TOKEN_INVALID` and marks current Token rejected; that same config is not retried. A new portal config revision (normally fresh Token) clears the latch and permits connection.

Network disconnects preserve last valid state except deliberate active-printer change, where state is cleared to prevent showing one printer's data under another printer's name.

# Shared concurrency

```text
Stock -> dedicated MarketDataWorker
Weather + HomeAssistant -> one shared AppDataWorker
Bambu -> one persistent MQTT service/task
DeviceInfo -> local-only
Bad Apple -> local flash playback
```

All ordinary short-lived external HTTP/TLS work serializes through `NetworkArbiter`. Bambu persistent MQTT holds it only for connect/reconnect handshake.

# Security

No Bambu/HA Token, password, verification code, Cookie, Authorization header or challenge material in logs/status. Portal itself is trusted-LAN HTTP; user must treat pasted Tokens as exposed to that LAN segment.
