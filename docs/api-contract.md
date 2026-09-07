# Data Provider / HTTP API Contract

Status date: 2026-09-07

Remote payloads stay behind provider/service abstractions. UI/controllers consume structured state and do not parse raw remote bodies directly. Local-only apps/media playback stay outside remote provider paths.

## Provider / service matrix

| Capability | Primary | Fallback / notes |
|---|---|---|
| A-share quote | Tencent | EastMoney |
| A-share intraday | Tencent | EastMoney |
| Weather | Open-Meteo | none |
| Home Assistant | user's existing HA server | read-only REST client |
| Bambu printer | Bambu Lab Cloud HTTPS + MQTT | persistent read-only background service |
| Weather Bad Apple | local compiled flash asset | no runtime network/task |
| Device Info | local device state | no network provider |

# A-share market data

Tencent quote/intraday remain primary; EastMoney remains fallback. Quote/intraday health are independent, retries are bounded, parsers fail closed and failures preserve last valid cache. Stock keeps its dedicated `MarketDataWorker`.

# Weather

Open-Meteo returns current weather plus three-day structured forecast. Default refresh 15 min, configurable 5–60 min, paced from last attempt, active-only via the shared AppDataWorker; failures preserve cache.

Presentation consumes only current + today + tomorrow. `dayAfter` remains valid provider/cache data but is not rendered.

## Bad Apple local media path

```text
pinned source video + verified Git blob SHA1
  -> ffmpeg 168x126 @ 10 FPS, 2190 frames
  -> threshold to 1-bit
  -> first frame + XOR sparse-delta encoding
  -> full delta round-trip verification
  -> generated BadAppleAsset.*
  -> firmware.bin
```

Runtime contract:

- viewport x=152, y=27, 168×126;
- 2190 frames @ 10 FPS, ~219 s, silent, loop;
- entry starts frame 0; exit stops scheduling/rendering;
- no full-width top divider and no vertical separator at the video boundary;
- one packed frame buffer = 2646 bytes plus small row buffer;
- no runtime HTTP/TLS, AppDataWorker request, NetworkArbiter acquisition or dedicated playback task.

# Home Assistant

The user's existing Home Assistant is the server. T-Display is only a read-only REST client.

For each configured entity:

```text
GET <existing-ha-base-url>/api/states/<entity_id>
Authorization: Bearer <long-lived-access-token>
Accept: application/json
```

Rules:

- 1–4 entities; sequential cycle;
- response entity ID must match configured ID;
- state required/bounded; optional friendly name/unit bounded;
- refresh 30–300 s, default 30 s, active-only;
- parse/fetch failure preserves prior per-entity state;
- no `/api/services` writes in V1.

HTTP mode uses ordinary `WiFiClient` and is trusted-LAN cleartext. HTTPS requires PEM CA with `WiFiClientSecure::setCACert()`; `setInsecure()` is forbidden in this credentialed path. Both modes acquire `NetworkArbiter`, retain bounded response bodies and never log Token/Authorization contents.

# Bambu Lab Cloud

## Role

Bambu is a device-level **read-only persistent integration**, not an AppDataWorker request and not tied to whether `BambuApp` is visible.

V1 intentionally excludes:

- printer LAN MQTT 8883 dependency/exposure;
- pause/resume/stop/light/temperature/camera control;
- control-command MQTT publishes.

## Configuration / credential lifecycle

Bambu configuration is stored in its own NVS namespace/blob, independent from AppConfig and HA config:

```text
enabled
region
email
password
accessToken
cloudUserId
printerSerial
printerName
```

`email` is retained as the storage/schema/form key for backward compatibility, but its semantic meaning is the **Bambu account identifier**. Validation is region-aware:

```text
CHINA  -> mainland phone account (11 digits, optional +86/86 prefix) OR email
US_EU  -> email
```

Password login must submit that identifier to Bambu Cloud as:

```json
{"account":"<identifier>","password":"<password>"}
```

The China phone path must not be blocked by HTML `type=email` validation or firmware email-only validation. The password/token remain secrets regardless of account identifier type.

The local shared Integrations portal is:

```text
http://<T-Display-IP>:8081/
```

Bambu routes:

```text
GET  /api/bambu/status
POST /api/bambu/login
POST /api/bambu/verify
POST /api/bambu/verification/resend
GET  /api/bambu/printers
POST /api/bambu/config
POST /api/bambu/logout
```

Status/config responses may expose only secret-presence booleans such as `password_set` and `token_set`; raw password/token values are forbidden. Verification status may expose only non-secret metadata such as whether verification is required, its typed channel (`sms`, `email`, `tfa`) and local resend delay. Password login/device-list/verification response bodies are not logged. Blank password preserves the stored password under the current portal merge semantics; logout clears password, access token, cloud user ID and printer selection.

Runtime ownership is explicit: `BambuMqttService` is the sole mutable owner of Bambu config after startup. Portal and UI obtain copies through mutex-protected snapshot/update APIs; no cross-core mutable `BambuConfig` reference is retained.

Portal-originated replacements are authoritative and increment an external config revision. Background HTTPS operations capture that revision together with their config snapshot. Any background writeback of refreshed token/user ID, auto-selected printer, or discovered-printer cache must compare the captured revision under the same mutex immediately before commit. If the current revision differs, the result is `STALE` and must be discarded without modifying NVS/runtime config/cache. This prevents an in-flight old Cloud response from reverting newer user configuration.

## Cloud HTTPS

Short-lived operations cover password login, verification-code submission, explicit SMS/email resend, user identity resolution and bound-printer discovery. They use `WiFiClientSecure` with CA bundle verification and acquire the shared `NetworkArbiter` for the complete request. `setInsecure()` is forbidden.

For China password accounts the same login endpoint accepts the phone identifier through JSON `account`; no email-specific transport endpoint is required for ordinary password login.

Bambu login responses are parsed into a typed authentication result. A Cloud challenge is represented as one of:

```text
SMS_CODE
EMAIL_CODE
TFA
```

The current challenge is owned by `BambuMqttService` and exists only in RAM while verification is pending. Challenge material such as `tfaKey` is never added to `BambuConfig`, never persisted to NVS and never returned by the status API. The one-time verification code supplied by the user is likewise never persisted or logged.

If a challenge is returned, the service enters `VERIFICATION_REQUIRED` and unattended relogin stops. The local `:8081` page then allows one-time code submission through `/api/bambu/verify`. SMS/email challenges may be explicitly resent through `/api/bambu/verification/resend`; resend is user-initiated only and guarded by a local 60-second cooldown. TFA challenges use the authenticator code and do not have a resend operation.

On successful verification, the replacement access token is revision-guarded and persisted first. The background service then resumes user-ID resolution, printer discovery/selection handling and Cloud MQTT connection. A subsequent reboot with a still-valid persisted token does not require another verification round unless Bambu Cloud challenges the account again.

Responses are bounded and parsed into narrow typed results. Password/token/verification code/challenge secrets/full auth bodies must not be printed to serial.

## Cloud MQTT

Broker selection:

```text
CHINA -> cn.mqtt.bambulab.com:8883
US_EU -> us.mqtt.bambulab.com:8883
```

Credentials/topics:

```text
username  = cloudUserId
password  = accessToken
subscribe = device/<printerSerial>/report
request   = device/<printerSerial>/request
```

After connection the service may publish exactly the read-only `pushall` state-sync request. It then consumes printer report messages. V1 must not publish printer-control commands.

The MQTT receive buffer is 40960 bytes. Allocation failure sets a non-fatal BUFFER_ERROR state. One service execution context exclusively owns connect/subscribe/callback state mutation and `mqtt.loop()`.

MQTT connect/reconnect handshake acquires `NetworkArbiter`; once established, the persistent dedicated TLS socket releases the arbiter and continues keepalive/report traffic independently. This is the deliberate exception to the normal “short-lived HTTP/TLS serialized through NetworkArbiter” rule.

## Printer state

`BambuState` retains at least:

```text
connected
normalized/gcode state
progress
remaining minutes
nozzle current/target
bed current/target
chamber temperature when available
layer current/total
job/subtask name
active filament/AMS information when available
lastUpdateMs
```

Malformed JSON fails without mutating the snapshot. Partial reports update only fields that are present and valid; absent or invalid fields preserve last valid values. Disconnects mark connectivity but do not clear the cached printer state.

## Token renewal / verification

MQTT authentication rc 4/5 or an otherwise invalid token moves the service into token-invalid/relogin flow. If a saved password exists, the service performs verified HTTPS login with the persisted account identifier, resolves/stores replacement user ID/token and reconnects MQTT when no further challenge is required.

Failed unattended attempts use bounded backoff:

```text
60 s -> 300 s -> 900 s -> 1800 s -> 1800 s ...
```

If relogin instead returns an SMS/email/TFA challenge, the service enters `VERIFICATION_REQUIRED` and stops unattended retry. The user can complete that one verification step locally through `:8081`; after success the new token is persisted and the normal background identity/discovery/MQTT path resumes. The firmware does not bypass second factors and does not automatically hammer resend endpoints.

# Shared worker / concurrency contract

```text
Stock -> dedicated MarketDataWorker
Weather + HomeAssistant -> exactly one shared AppDataWorker
Bambu -> dedicated persistent MQTT service/task
DeviceInfo -> local-only
Bad Apple -> local flash playback
```

The shared AppDataWorker has typed WEATHER / HOME_ASSISTANT result queues so delayed results cannot cross-consume. FreeRTOS queues pass pointers to C++ request/result objects rather than byte-copying objects containing `std::string`.

Bambu config concurrency uses a single-owner + revision/CAS-style rule: externally submitted Portal config wins over any older in-flight background derivation. Internal token/user/printer persistence is allowed only when the external revision still equals the revision captured with the source snapshot.

All ordinary short-lived external HTTP/TLS work serializes through `NetworkArbiter`. Bambu persistent MQTT holds it only during connect/reconnect handshake, never for the session lifetime.

# Integrations portal

Exactly one `WebServer{8081}` exists: `IntegrationConfigPortal`.

HA routes remain compatible:

```text
/api/ha/status
/api/ha/config
```

Bambu routes are listed above. The local portal itself is HTTP and therefore trusted-LAN-only. Verification UI is rendered only while the service reports a pending challenge.

# Diagnostics / cache isolation

```text
[md]      Stock market data
[appdata] WEATHER / HOME_ASSISTANT
[net]     short-lived transport; HA mode=HA_HTTP|HA_CA where applicable
[sys]     MENU|STOCK|WEATHER|BAMBU|HOME_ASSISTANT|DEVICE_INFO
```

No credentials/full auth bodies/verification codes/challenge secrets in logs.

Weather failure cannot alter Stock health; Stock failures cannot clear Weather/HA/Bambu caches; HA failures preserve entity cache; Bambu network loss preserves last printer snapshot; inactive Weather/HA late results never redraw the current app; stale Bambu background Cloud results cannot overwrite newer Portal configuration; Bad Apple cannot change provider/network health.