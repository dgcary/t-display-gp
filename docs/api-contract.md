# Data Provider / HTTP API Contract

Status date: 2026-09-08

Remote payloads stay behind provider/service abstractions. UI consumes structured state.

## Providers

- A-share: Tencent primary / EastMoney fallback
- Weather: Open-Meteo
- Home Assistant: existing HA REST API
- Bambu: Manual Token HTTPS + persistent per-printer Cloud MQTT
- Bad Apple: local compiled asset
- Device Info: local state

# Home Assistant

Read-only `GET /api/states/<entity_id>`. HTTP only on trusted LAN; HTTPS requires configured CA; no service writes or secret echo.

# Bambu Lab Cloud

## Config / portal

Schema v2: `enabled, region, accessToken, cloudUserId, printers[4]{serial,name}, printerCount, activePrinterIndex`. Enabled config requires Token + User ID + >=1 printer. Max 4 configured entries, unique safe serials. Legacy account/password auth stays retired.

One `WebServer{8081}` serves HA plus:

```text
GET  /api/bambu/status
GET  /api/bambu/printers
POST /api/bambu/discover
POST /api/bambu/config
POST /api/bambu/logout
```

Status contains non-secret metadata only. Printer list is local saved data. Discovery is explicit and Save-gated. Blank Token preserves stored Token. Old login/verify/resend endpoints unsupported.

## Active printer control

```text
GPIO0 PREV_SHORT  -> cycleActivePrinter(-1)
GPIO14 NEXT_SHORT -> cycleActivePrinter(+1)
```

`activePrinterIndex` is presentation selection. With an unchanged connection set, switching persists the new index but does **not** increment a connection-affecting revision, disconnect sockets, clear sibling slot cache, or perform Cloud discovery/login. `snapshot()` and `status()` read the selected slot.

## Token HTTPS client

`BambuCloudClient`: `fetchUserId(token,region)` and `fetchPrinters(token,region)` only. Strict CA + shared `NetworkArbiter`; no `setInsecure()`.

## MQTT protocol / ownership

```text
CHINA -> cn.mqtt.bambulab.com:8883
US_EU -> us.mqtt.bambulab.com:8883
username = cloudUserId
password = accessToken
per slot subscribe = device/<serial>/report
per slot request   = device/<serial>/request
```

Only read-only `pushall` may be published.

MQTT is main-loop driven: `begin(config,store)` initializes; Arduino `loop()` calls `process(nowMs)`. There is no custom FreeRTOS `bambu-mqtt` task or CPU0 pinning.

### Persistent multi-printer slots

Runtime contains one `MqttConn` and one `BambuState` cache per configured slot (array capacity 4). Each slot independently owns:

```text
WiFiClientSecure
PubSubClient
status / tokenRejected
last attempt / backoff counter
connect time / delayed pushall
pushall sequence
BambuState cache
```

Connected slots are serviced first every loop pass. If one or more slots need connection, `process()` starts at most one blocking attempt per pass, preferring active slot then siblings. Callback routes `device/<serial>/report` to the matching slot by topic/Serial.

Per-slot connection attempt:

```text
release stale objects for that slot only
acquire NetworkArbiter
new WiFiClientSecure + CA bundle + setTimeout(15)
new PubSubClient + 40960 buffer + keepalive 30
random bblp_* client ID
mqtt.connect(clientId, cloudUserId, accessToken)
subscribe device/<slot serial>/report
release NetworkArbiter
>=2000 ms later -> one read-only pushall for that slot
```

Per-slot backoff: failures 0..4 →30 s; 5..14 →60 s; >=15 →120 s. rc 4/5 latches rejected-token state. Failure/reconnect of one slot must not tear down sibling online slots.

A config save that changes only active index and/or printer names preserves connection objects and cached state. Credential, region, enablement, printer count or serial-set changes are connection-affecting and rebuild runtime.

The Stage-2/3/4 explicit `mqtt_real_tls_*`, layered preflight and project `MQTT_SOCKET_TIMEOUT` overrides remain retired. PubSubClient owns TCP/TLS establishment. Strict CA required; `setInsecure()` forbidden.

`esp_task_wdt_reset()` may be used around known long connect/callback/publish operations, matching BambuHelper; disabling/deleting watchdogs is forbidden.

Secret-safe logs use `slot=<n>` and may include rc/elapsed/RSSI/heap/backoff; never Token, Cloud User ID, Cookie/Authorization or auth payload.

## Presentation / concurrency

BambuScreen uses partial redraw; full display clear only explicit full redraw. Active-slot change can full redraw presentation but must not imply MQTT teardown.

```text
Stock -> dedicated MarketDataWorker
Weather + HA -> shared AppDataWorker
Bambu -> loop-driven persistent per-printer MQTT slots
DeviceInfo -> local-only
```

Short-lived HTTP/TLS and new Bambu connection transactions use `NetworkArbiter`; established MQTT sockets do not hold it.

Code capacity is 4 slots. Current physical acceptance target is 2 simultaneous real printers; 4 simultaneous connections require separate hardware/resource validation.
