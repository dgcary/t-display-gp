# Bambu Cloud Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a read-only Bambu Lab Cloud printer monitor with saved-password automatic token renewal, persistent Cloud MQTT state, device selection, and a native T-Display-S3 UI.

**Architecture:** Bambu is a device-level integration, not an `AppDataWorker` request. Pure config/protocol/state logic remains host-testable; short HTTPS login/device-discovery operations use strict CA verification plus `NetworkArbiter`; one dedicated MQTT service maintains a verified-TLS persistent socket and publishes state snapshots to a passive `BambuApp` renderer.

**Tech Stack:** Arduino/C++17, ESP32-S3 Arduino core via PlatformIO, ArduinoJson 6.21.6, PubSubClient 2.8, Preferences/NVS, WiFiClientSecure, TFT_eSPI, Unity native tests, Python contract validators, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-09-04-bambu-cloud-integration-design.md`

## Global Constraints

- Final menu order: Stock, Weather, Bambu Lab, Home Assistant, Device Info.
- Startup = Stock; no automatic idle switching.
- Bambu V1 is Cloud-first only; do not expose or depend on printer LAN MQTT 8883.
- Save email/password/token/user ID/selected printer in Bambu-owned NVS; never hard-code or log password/token.
- Password-based automatic renewal is required for non-2FA accounts; 2FA requirement must fail explicitly without bypass attempts.
- Bambu Cloud HTTPS and MQTT must use CA verification; `setInsecure()` is forbidden in Bambu credential paths.
- Bambu V1 is read-only: no pause/resume/stop/light/temperature/camera controls.
- MQTT receive capacity >= 40960 bytes and allocation failure must be non-fatal.
- MQTT is persistent across app transitions and serviced from exactly one execution context.
- Only connect/reconnect handshakes use `NetworkArbiter`; a successful persistent MQTT session does not hold it for its lifetime.
- Preserve Stock, Weather+Bad Apple, HA, DeviceInfo and application-only flashing behavior.
- Any adapted BambuHelper MIT code/reference must be acknowledged in `THIRD_PARTY_NOTICES.md`.

---

### Task 1: Bambu configuration model and secret-safe persistence contract

**Files:**
- Create: `src/network/BambuConfig.h`
- Create: `src/network/BambuConfig.cpp`
- Create: `test/test_bambu_config/test_main.cpp`
- Modify: `platformio.ini`

**Interfaces:**
- Produces `enum class BambuRegion { US_EU, CHINA };`
- Produces `struct BambuConfig { bool enabled; BambuRegion region; std::string email; std::string password; std::string accessToken; std::string cloudUserId; std::string printerSerial; std::string printerName; };`
- Produces `validateBambuConfig`, `BambuConfigCodec::encode/decode`, `bambuBrokerForRegion`.

- [ ] **Step 1: Write native RED tests.**

Cover disabled/default config, valid US/EU and China config, length/format rejection, encode/decode round-trip including secrets, malformed JSON fail-closed, and broker mapping:

```cpp
TEST_ASSERT_EQUAL_STRING("us.mqtt.bambulab.com", bambuBrokerForRegion(BambuRegion::US_EU));
TEST_ASSERT_EQUAL_STRING("cn.mqtt.bambulab.com", bambuBrokerForRegion(BambuRegion::CHINA));
```

- [ ] **Step 2: Run CI/native and verify RED because BambuConfig symbols do not exist.**

- [ ] **Step 3: Implement bounded config validation/codec.**

Recommended maxima:

```text
email 160 bytes
password 256 bytes
access token 1536 bytes
cloud user id 96 bytes
printer serial 32 bytes
printer name 64 bytes
encoded config 4096 bytes
```

A disabled config may be incomplete. An enabled config must have email plus either stored password or token; selected printer is allowed to be empty while setup is incomplete.

- [ ] **Step 4: Add pure source to native build and verify PASS.**

- [ ] **Step 5: Commit.**

```bash
git commit -m "feat: add Bambu Cloud configuration model"
```

---

### Task 2: Pure Cloud protocol parsing and printer discovery

**Files:**
- Create: `src/network/BambuCloudProtocol.h`
- Create: `src/network/BambuCloudProtocol.cpp`
- Create: `test/test_bambu_cloud_protocol/test_main.cpp`
- Modify: `platformio.ini`

**Interfaces:**
- `BambuLoginParseResult parseBambuLoginResponse(int httpStatus, std::string_view body)`
- `bool extractBambuCloudUserId(std::string_view token, std::string& out)`
- `bool parseBambuPrinterList(std::string_view body, std::vector<BambuPrinterInfo>& out)`
- `std::string bambuReportTopic(std::string_view serial)`
- login outcomes: `SUCCESS`, `INVALID_CREDENTIALS`, `TWO_FACTOR_REQUIRED`, `MALFORMED`, `SERVICE_ERROR`.

- [ ] **Step 1: Write RED fixtures for successful login, invalid password, TFA request, malformed reply, JWT user ID extraction, device bind list and topic construction.**

Representative expected topic:

```cpp
TEST_ASSERT_EQUAL_STRING("device/01P00A000000000/report", bambuReportTopic("01P00A000000000").c_str());
```

- [ ] **Step 2: Verify RED.**

- [ ] **Step 3: Implement strict parsers using ArduinoJson.**

Do not log or retain raw response bodies beyond parsing. Device list parser retains only bounded serial/name fields.

- [ ] **Step 4: Verify native PASS.**

- [ ] **Step 5: Commit.**

```bash
git commit -m "feat: parse Bambu Cloud auth and printer data"
```

---

### Task 3: Automatic renewal/backoff state machine

**Files:**
- Create: `src/network/BambuSessionModel.h`
- Create: `src/network/BambuSessionModel.cpp`
- Create: `test/test_bambu_session_model/test_main.cpp`
- Modify: `platformio.ini`

**Interfaces:**
- States: `DISABLED`, `UNCONFIGURED`, `MQTT_CONNECTING`, `ONLINE`, `TOKEN_INVALID`, `RELOGIN_PENDING`, `RELOGIN_IN_PROGRESS`, `TWO_FACTOR_REQUIRED`, `LOGIN_FAILED`, `NETWORK_ERROR`, `BUFFER_ERROR`.
- `BambuSessionModel::onMqttAuthFailure(nowMs)`
- `BambuSessionModel::shouldRelogin(nowMs)`
- `BambuSessionModel::onReloginSuccess()`
- `BambuSessionModel::onReloginFailure(nowMs, reason)`

- [ ] **Step 1: Write RED tests for 1m/5m/15m/30m capped backoff, success reset, 2FA terminal behavior and millis wrap-safe scheduling.**

- [ ] **Step 2: Verify RED.**

- [ ] **Step 3: Implement deterministic model with unsigned elapsed arithmetic.**

Backoff sequence after failed automatic attempts:

```text
60 s -> 300 s -> 900 s -> 1800 s -> 1800 s ...
```

- [ ] **Step 4: Verify PASS and commit.**

```bash
git commit -m "feat: add Bambu automatic relogin state model"
```

---

### Task 4: Printer state parser

**Files:**
- Create: `src/network/BambuState.h`
- Create: `src/network/BambuState.cpp`
- Create: `test/test_bambu_state/test_main.cpp`
- Modify: `platformio.ini`

**Interfaces:**
- `BambuState` includes connection/state/progress/ETA/temps/layers/job/active filament/lastUpdate.
- `bool applyBambuReport(std::string_view json, uint32_t nowMs, BambuState& state)`.

- [ ] **Step 1: Write RED tests with RUNNING, PAUSE, FINISH, partial delta, malformed JSON, invalid ranges and basic AMS active-tray fixtures.**

- [ ] **Step 2: Verify RED.**

- [ ] **Step 3: Implement partial-update parsing.**

Rules:
- parse into temporary per-field values;
- update only fields that are present and valid;
- malformed entire JSON returns false without mutating state;
- absent fields preserve last valid values;
- progress outside 0..100 is ignored;
- strings are bounded before assignment.

- [ ] **Step 4: Verify PASS and commit.**

```bash
git commit -m "feat: parse Bambu printer status reports"
```

---

### Task 5: Secret store and verified HTTPS Cloud client

**Files:**
- Create: `src/network/BambuConfigStore.h`
- Create: `src/network/BambuConfigStore.cpp`
- Create: `src/network/BambuCloudClient.h`
- Create: `src/network/BambuCloudClient.cpp`
- Modify: `include/build_config.h`
- Modify: `platformio.ini`

**Interfaces:**
- `BambuConfigStore::load/save/clear` using a Bambu-owned NVS namespace/key.
- `BambuCloudClient::login(email,password,region)`
- `BambuCloudClient::fetchUserId(token,region)`
- `BambuCloudClient::fetchPrinters(token,region)`

- [ ] **Step 1: Add compile/static RED contract before live transport implementation.**

The Bambu validator added later must reject `setInsecure()` in every Bambu Cloud source.

- [ ] **Step 2: Implement NVS persistence.**

Use a separate namespace such as `bambucloud`; normal firmware upgrade must preserve it. Status/logging helpers never serialize password/token.

- [ ] **Step 3: Implement strict verified HTTPS.**

Adapt only the BambuHelper protocol behavior required for password login/profile/device-bind queries. Use root CA bundle support or an explicitly pinned trusted CA path supported by the current Arduino core; no insecure fallback. Each HTTPS operation acquires/releases `NetworkArbiter` around its complete request.

- [ ] **Step 4: Keep response sizes bounded and return typed errors.**

No cloud response body or Authorization credential is printed to serial.

- [ ] **Step 5: Real ESP32 compile.**

If Arduino core 2.0.14 cannot embed/use the certificate bundle as expected, stop and resolve the trust-chain implementation before proceeding; do not substitute `setInsecure()`.

- [ ] **Step 6: Commit.**

```bash
git commit -m "feat: add verified Bambu Cloud authentication client"
```

---

### Task 6: Persistent Cloud MQTT service

**Files:**
- Create: `src/network/BambuMqttService.h`
- Create: `src/network/BambuMqttService.cpp`
- Modify: `platformio.ini`
- Modify: `include/build_config.h`

**Interfaces:**
- `bool begin(BambuConfig&, BambuConfigStore&, BambuCloudClient&)`
- `void process(uint32_t nowMs, bool wifiOnline)` or an owned single FreeRTOS task, but never both.
- `BambuState snapshot() const` or a lock-protected const-copy API.
- `BambuSessionStatus status() const`.

- [ ] **Step 1: Add `knolleary/PubSubClient@^2.8`.**

- [ ] **Step 2: Configure MQTT buffer >=40960 bytes.**

Use `PubSubClient::setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)` and handle false/allocation failure by setting `BUFFER_ERROR`, not rebooting.

- [ ] **Step 3: Implement verified-TLS broker selection and connection.**

Credentials:

```text
username = cloudUserId
password = accessToken
broker = cn.mqtt.bambulab.com or us.mqtt.bambulab.com
subscribe = device/<serial>/report
```

Acquire `NetworkArbiter` only around TLS/MQTT connect/reconnect. Release after successful or failed handshake.

- [ ] **Step 4: Enforce one MQTT execution context.**

Only that context invokes `connect`, `subscribe`, callback-state mutation and `mqtt.loop()`.

- [ ] **Step 5: Connect auth failures to automatic relogin.**

MQTT rc 4/5 -> token invalid -> if saved password and no 2FA terminal state, perform bounded automatic HTTPS login -> persist new token/user ID -> reconnect MQTT.

- [ ] **Step 6: Preserve cached state across disconnects.**

Network loss marks connectivity offline/connecting but does not clear last valid print snapshot.

- [ ] **Step 7: Commit after ESP32 compile succeeds.**

```bash
git commit -m "feat: maintain Bambu Cloud MQTT session"
```

---

### Task 7: Bambu app and 320×170 screen

**Files:**
- Create: `src/app/BambuApp.h`
- Create: `src/app/BambuApp.cpp`
- Create: `src/ui/BambuScreen.h`
- Create: `src/ui/BambuScreen.cpp`
- Modify: `src/app/AppShell.h`
- Modify: `src/main.cpp`

**Interfaces:**
- Add `AppId::BAMBU` between WEATHER and HOME_ASSISTANT.
- `BambuApp` reads service snapshot/status only; it never owns/disconnects MQTT.

- [ ] **Step 1: Add RED app-shell/static contract for final five-app menu and `BAMBU` diagnostics.**

- [ ] **Step 2: Add app wiring.**

Final menu:

```cpp
股票
天气
Bambu Lab
智能家居
设备信息
```

Startup remains Stock.

- [ ] **Step 3: Render the V1 status screen.**

Prioritize printer name/state, large percentage + bar, ETA, layer, nozzle/bed/chamber, job, active filament. Make OFFLINE/CONNECTING/2FA/LOGIN FAILED visible without crashing navigation.

- [ ] **Step 4: Keep app transitions passive.**

`onEnter/onExit` only affect redraw; they never connect/disconnect MQTT.

- [ ] **Step 5: Compile and commit.**

```bash
git commit -m "feat: add Bambu printer monitor app"
```

---

### Task 8: Generalize port 8081 into Integrations portal

**Files:**
- Create: `src/network/IntegrationConfigPortal.h`
- Create: `src/network/IntegrationConfigPortal.cpp`
- Delete: `src/network/HomeAssistantConfigPortal.h`
- Delete: `src/network/HomeAssistantConfigPortal.cpp`
- Modify: `src/main.cpp`
- Modify: current HA validator/tests as needed
- Create: `test/test_bambu_portal_model/test_main.cpp` if secret-preservation form logic is extracted to pure helpers.

**Interfaces:**
- Preserve existing `/api/ha/status` and `/api/ha/config` behavior.
- Add `/api/bambu/status`, `/api/bambu/login`, `/api/bambu/printers`, `/api/bambu/config`, `/api/bambu/logout`.

- [ ] **Step 1: Extract/test pure form merge semantics.**

Blank password preserves existing stored password. Explicit logout clears password/token/user ID/printer selection. Status serialization may return only `password_set`/`token_set`, never the values.

- [ ] **Step 2: Build one `WebServer{8081}` Integrations page.**

HA section remains operational. Bambu section provides region, email, password, remember-password, current login/MQTT state and printer selection.

- [ ] **Step 3: Implement login and device picker flow.**

Login success persists token/user ID, fetches printers, and lets the user save one serial/name. Login failure/2FA produces explicit UI status.

- [ ] **Step 4: Secret leak checks.**

Search source/status JSON/serial strings for any password/token output paths.

- [ ] **Step 5: Compile and commit.**

```bash
git commit -m "feat: add Bambu Cloud integration portal"
```

---

### Task 9: Static security/architecture contract and third-party notices

**Files:**
- Create: `tools/validate_bambu_cloud_contract.py`
- Modify: `.github/workflows/ci.yml`
- Modify: `THIRD_PARTY_NOTICES.md`

**Interfaces:**
- Validator enforces final architecture/security rather than live service availability.

- [ ] **Step 1: Validator requires all Bambu app/config/cloud/MQTT/state/portal files and `AppId::BAMBU`.**

- [ ] **Step 2: Validator rejects Bambu `setInsecure()`, raw token/password status keys, printer-control MQTT actions, missing `NetworkArbiter` use on login/MQTT connect, and MQTT buffer below 40960.**

- [ ] **Step 3: Add validator to CI.**

- [ ] **Step 4: Add notices for `Keralots/BambuHelper` MIT reference and `knolleary/PubSubClient` MIT dependency.**

- [ ] **Step 5: Verify validators/native/ESP build.**

- [ ] **Step 6: Commit.**

```bash
git commit -m "test: enforce Bambu Cloud security contract"
```

---

### Task 10: Align docs and hardware acceptance

**Files:**
- Modify: `README.md`
- Modify: `AGENTS.md`
- Modify: `docs/deployment.md`
- Modify: `docs/api-contract.md`
- Modify: `docs/hardware-acceptance.md`

- [ ] **Step 1: Document final five-app shell and Stock startup.**

- [ ] **Step 2: Document Bambu Cloud credential lifecycle, strict TLS, automatic password relogin and 2FA limitation.**

- [ ] **Step 3: Document background MQTT concurrency and 40 KiB buffer.**

- [ ] **Step 4: Add physical acceptance for remote printer Cloud MQTT, app-background freshness, token invalidation/relogin, Wi-Fi recovery and no secret leak.**

- [ ] **Step 5: Commit.**

```bash
git commit -m "docs: document Bambu Cloud monitor"
```

---

### Task 11: Final exact-head verification and Draft PR

**Files:** none unless verification finds a defect.

- [ ] **Step 1: Run exact-head GitHub Actions.**

Required PASS:

```text
all static validators
all Ubuntu native tests
Windows native tests
Bad Apple 2190-frame generation + round-trip
ESP32-S3 PlatformIO build
```

- [ ] **Step 2: Download the CI artifact and independently recompute ZIP, firmware, partitions and bootloader SHA256.**

- [ ] **Step 3: Open/update a Draft PR from `feature/bambu-cloud` to `feature/weather-bad-apple`.**

Do not merge based on CI. Physical Bambu account/MQTT evidence is required.

- [ ] **Step 4: Produce the Codex smoke instructions.**

Codex flashes only exact `firmware.bin` at manifest offset, preserves NVS, configures Bambu account through `:8081`, selects the remote printer, confirms MQTT data outside printer LAN, checks password/token secrecy, tests background freshness and Wi-Fi recovery, and returns `[bambu]`, `[net]`, `[sys]`, `[md]`, `[appdata]` evidence.
