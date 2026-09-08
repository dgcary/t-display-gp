# Bambu Cloud Integration Design

**Date:** 2026-09-04  
**Base:** `feature/weather-bad-apple` at `c8d097594b943e023f8c23175861d0e14b730fae`  
**Reference implementation:** `Keralots/BambuHelper` pinned at `f86555c4e050ccee73d8005ac5dfc77baa101b5c`

## Goal

Replace the low-value Nixie Clock and Crypto apps with a practical Bambu Lab printer monitor that works when the T-Display-S3 is not on the printer's LAN.

Final app shell:

```text
Stock
Weather + Bad Apple
Bambu Lab
Home Assistant
Device Info
```

Startup returns to **Stock**. There is no automatic idle app switching.

## Non-goals for V1

- No public exposure of the printer's local MQTT port 8883.
- No LAN MQTT mode in V1.
- No printer control commands: no pause/resume/stop, no temperature changes, no light control.
- No camera/video stream.
- No complete BambuHelper UI, OTA, Wi-Fi manager, or web portal import.
- No full AMS grid or HMS diagnostic browser in the first screen.
- No email-code login flow as the normal user experience.

## Why Cloud-first

The device may be outside the printer LAN. BambuHelper already demonstrates the required cloud flow:

1. Authenticate a Bambu account over HTTPS.
2. Obtain and store an access token.
3. Resolve the cloud user ID.
4. Fetch the account-bound printer list.
5. Select and persist one printer serial number.
6. Connect to Bambu Cloud MQTT over TLS.
7. Subscribe to `device/<serial>/report` and maintain current printer state.

Broker selection follows the reference implementation:

- China region: `cn.mqtt.bambulab.com`
- US/EU: `us.mqtt.bambulab.com`

Cloud MQTT credentials are the resolved cloud user ID as username and access token as password.

## Authentication and automatic renewal

### First setup

The integrated configuration portal gains a **Bambu Lab Cloud** section. V1 asks for:

- region: China or US/EU
- account email
- account password
- `remember password / automatic renewal` enabled by default

On successful sign-in the firmware stores:

- region
- email
- password
- access token
- cloud user ID
- selected printer serial and friendly name

The portal then fetches the account-bound printer list and lets the user select one printer.

### Normal boot

1. Load saved Bambu configuration.
2. If a token, cloud user ID and selected serial exist, start Cloud MQTT directly.
3. Do not perform an account login on every boot when the existing token still works.

### Expired/invalid token

MQTT authentication failures equivalent to CONNACK 4/5 mark the token invalid.

If a saved password exists, the firmware automatically signs in again, stores the replacement token/user ID, refreshes the printer list if needed, and reconnects MQTT.

Automatic login attempts are bounded with backoff so a wrong password, account lockout, service outage, or API change cannot hammer Bambu Cloud continuously. Recommended schedule after the first failed automatic re-login: 1 min, 5 min, 15 min, then 30 min maximum between attempts.

If the Bambu account requires a second factor, password-only unattended renewal cannot complete. V1 must detect this condition and show a clear `2FA required / automatic renewal unavailable` status rather than repeatedly retrying or attempting to bypass 2FA.

A manual access-token field may remain as an advanced recovery path, but it is not the primary setup flow.

## Credential security

Bambu credentials live in a separate NVS namespace/blob, independent from `AppConfig` and Home Assistant configuration.

Required secret handling:

- password and access token are never hard-coded or committed;
- password/token are never printed to serial;
- password/token are never returned by status/config GET endpoints;
- portal status exposes only booleans such as `password_set` and `token_set`;
- blank password input means preserve the stored password unless the user explicitly clears credentials;
- logout/clear action erases password, token, cloud user ID and printer selection;
- cloud HTTPS and MQTT credential paths use CA verification; `setInsecure()` is forbidden for Bambu Cloud;
- login/device-list response bodies are not logged by default.

## Components

### `BambuConfig`

Owns persisted settings and validation:

```text
region
email
password
accessToken
cloudUserId
printerSerial
printerName
enabled
```

It is stored separately from the existing HA configuration.

### `BambuCloudClient`

Owns short-lived HTTPS operations:

- password sign-in;
- detect 2FA-required response;
- resolve cloud user ID;
- fetch bound printer list;
- sanitize/validate all responses;
- never expose secrets to logs.

All short-lived Bambu Cloud HTTPS calls acquire the existing `NetworkArbiter` so login/device-discovery handshakes do not collide with Stock/Weather/HA TLS setup.

The implementation may adapt the BambuHelper cloud protocol code, but must not import its display system, OTA, Wi-Fi manager, settings portal, or unrelated features. Any adapted MIT-licensed code must retain appropriate attribution/license notice.

### `BambuMqttService`

MQTT is a persistent connection and therefore must not be modeled as an `AppDataWorker` request.

The service owns exactly one `WiFiClientSecure` and one `PubSubClient` connection for the selected printer. It runs independently of `BambuApp` so printer state can stay current while another app is visible.

Connection/reconnection handshake acquires `NetworkArbiter`; after a successful persistent connection the arbiter is released. The service owns its MQTT socket exclusively and services `mqtt.loop()` from one execution context only.

Reconnect policy uses bounded exponential/backoff behavior. Network loss does not clear the last valid printer state.

For large Bambu status messages the MQTT receive buffer must support at least **40 KiB**, matching the practical requirement in the reference implementation for printers with large AMS payloads. The implementation must record allocation failure cleanly and must not crash if sufficient memory is unavailable.

### `BambuState`

V1 keeps a compact read-only state model with at least:

```text
connected
gcodeState / normalized state
progress 0..100
remainingMinutes
nozzleTemp / nozzleTarget
bedTemp / bedTarget
chamberTemp when available
layerNum / totalLayers
subtaskName or current job name
active filament type/color/slot when available
lastUpdateMs
```

Parsing is fail-closed: malformed or partial MQTT payloads update only fields that are valid, and a parse failure never destroys the last valid snapshot.

### `BambuApp` / `BambuScreen`

The app is a renderer/controller over `BambuState`; it does not own the MQTT connection.

Target 320×170 screen prioritizes:

- printer friendly name + connection/print state;
- large print progress and progress bar;
- ETA;
- layer current/total;
- nozzle, bed and chamber temperatures;
- job name;
- current filament/AMS slot when available.

State badges include at least IDLE, PRINTING, PAUSE, PREPARE, FINISH, FAILED, OFFLINE/CONNECTING.

V1 is read-only. No screen action publishes printer-control MQTT commands.

## Configuration portal

Do not open a second web server/port.

The existing device integration portal on `http://<device-ip>:8081/` is generalized from an HA-only page into a shared **Integrations** page containing:

- Home Assistant section, preserving current behavior and secrets;
- Bambu Lab Cloud section;
- login/connect status;
- printer picker populated from the Bambu account after successful authentication;
- explicit logout/clear action.

The existing HA API routes may remain for compatibility. Bambu-specific routes use a separate prefix such as `/api/bambu/...`.

## Network concurrency

Existing HTTP/TLS operations remain serialized through `NetworkArbiter`.

Bambu introduces one deliberate exception: once the Cloud MQTT TLS connection is established, its small keepalive/report traffic remains on its dedicated persistent socket and does not hold `NetworkArbiter` for the lifetime of the connection. Only MQTT connect/reconnect handshakes participate in arbiter acquisition.

This prevents a permanent MQTT session from starving Stock, Weather or Home Assistant while still avoiding simultaneous expensive TLS handshakes.

No app transition may disconnect MQTT. Bambu Cloud is a device-level background integration, not an active-app-only fetcher.

## App-shell cleanup

Remove Crypto completely:

- `CryptoApp`, controller, screen, provider/model as applicable;
- Binance request/result types and scheduling from `AppDataWorker`;
- Crypto tests and validator clauses;
- `AppId::CRYPTO`, menu item and diagnostics;
- Crypto documentation/config references.

Remove Nixie completely:

- `NixieClockApp`, screen/model;
- Nixie tests/validator;
- `AppId::NIXIE_CLOCK`, menu item and diagnostics;
- Nixie documentation/config references.

Retain the common system clock/NTP facilities if other code depends on them.

Set startup app to **Stock**.

Final diagnostics app names:

```text
MENU | STOCK | WEATHER | BAMBU | HOME_ASSISTANT | DEVICE_INFO
```

## Error handling and status

Bambu state must distinguish at least:

- disabled/unconfigured;
- signing in;
- login failed;
- 2FA required;
- fetching printers;
- no printer selected;
- MQTT connecting;
- online;
- token expired / auto-renewing;
- network unavailable;
- MQTT buffer/allocation failure.

A service error must not reboot the board, clear valid configuration, or block navigation.

## Testing strategy

Implementation follows TDD.

Native/unit tests cover:

- Crypto/Nixie removal and final menu/startup contract;
- Bambu config validation and secret-preservation semantics;
- login response parsing including successful token, invalid credentials and 2FA-required cases;
- user ID extraction/fallback response parsing;
- printer-list parsing and selected-printer persistence;
- MQTT topic construction and region-to-broker mapping;
- normalized printer-state parsing, partial updates and malformed payload preservation;
- token-invalid -> automatic re-login state machine and retry backoff;
- no secret values in diagnostic/status serialization.

Static contract validation asserts:

- Bambu Cloud credential paths never call `setInsecure()`;
- Bambu app is read-only and does not publish control commands;
- MQTT buffer target is at least 40 KiB;
- final app shell contains exactly Stock, Weather, Bambu, Home Assistant and Device Info;
- startup is Stock and idle auto-switch remains absent.

ESP32-S3 CI must perform a real PlatformIO build and publish an exact-SHA artifact as before.

## Physical acceptance

On the real T-Display-S3:

1. Existing Wi-Fi/NVS, Stock, Weather+Bad Apple, HA and Device Info regressions pass.
2. Startup opens Stock.
3. Menu contains exactly five apps in the intended order.
4. Bambu portal can save account credentials without echoing password/token.
5. Password login succeeds for a non-2FA account and device list is populated.
6. Selected remote printer reaches Cloud MQTT without LAN reachability to the printer.
7. During an active print, progress/ETA/temps/layers/job/filament update without UI freeze.
8. Leaving Bambu app does not stop MQTT state updates; returning shows fresh cached state.
9. Stock/Weather/HA remain usable while MQTT stays connected.
10. Wi-Fi loss/recovery reconnects without watchdog, panic, reboot or monotonic heap loss.
11. Invalid/expired token triggers password-based automatic re-login and reconnect when the account does not require 2FA.
12. At least 100 app transitions remain stable.

## Rollout

Implementation is split into two independently testable plans:

1. **App-shell cleanup:** remove Crypto/Nixie, restore Stock startup, update tests/docs, build and verify.
2. **Bambu Cloud integration:** config/auth/device discovery, persistent MQTT service, state parser, UI, portal, security tests and physical acceptance.

The Bambu PR remains Draft until physical Cloud authentication/MQTT evidence is collected on the user's real printer/account. Do not merge based on CI alone.
