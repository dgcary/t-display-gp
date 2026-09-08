# BambuHelper-Aligned MQTT Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the experimental CPU0-pinned Bambu MQTT runtime with a high-fidelity adaptation of Keralots/BambuHelper's proven Cloud MQTT lifecycle and loop-driven scheduling.

**Architecture:** `BambuMqttService` becomes an initialization + main-loop-driven service. Its normal reconnect path directly constructs `WiFiClientSecure` + `PubSubClient`, lets PubSubClient own the TLS connect, delays initial pushall by 2 seconds, fully rebuilds clients on failure, and uses BambuHelper-style Cloud backoff. The ESP32 PlatformIO platform is aligned to 6.12.0 while all Manual Token, local multi-printer, read-only and UI contracts remain intact.

**Tech Stack:** ESP32-S3, Arduino-ESP32 2.0.17 via PlatformIO espressif32 6.12.0, C++17, PubSubClient 2.8, WiFiClientSecure, FreeRTOS mutexes, Unity/native tests, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-09-08-bambuhelper-runtime-alignment-design.md`

## Global Constraints

- Target remains LILYGO T-Display-S3, logical landscape 320x170, existing pins/orientation unchanged.
- Bambu remains Cloud-only, Manual Token, strict CA and read-only; only `pushall` may be published.
- Keep existing Bambu config schema v2, portal routes, active-printer persistence and anti-flicker UI behavior.
- No password/SMS/TFA/automatic token renewal.
- No `setInsecure()` in Bambu Cloud MQTT.
- Never log Access Token, Cloud User ID, Cookie or Authorization values.
- Normal deployment preserves NVS and flashes application only at `0x10000`.

---

### Task 1: Add the reference-runtime RED contract

**Files:**
- Modify: `tools/validate_bambu_pubsub_timeout_contract.py`

**Interfaces:**
- Consumes: current `platformio.ini`, `src/network/BambuMqttService.{h,cpp}`, `src/main.cpp`.
- Produces: a static validator that fails until the BambuHelper-aligned runtime exists.

- [ ] **Step 1: Replace the Stage-4 diagnostic assertions with the new runtime assertions**

Require these exact behaviors:

```text
platform = espressif32@6.12.0
no -DMQTT_SOCKET_TIMEOUT
public process(uint32_t nowMs)
main.cpp calls bambuMqttService.process(nowMs)
no xTaskCreatePinnedToCore / taskThunk / taskLoop / "bambu-mqtt"
setCACertBundle(rootca_crt_bundle_start)
tls_->setTimeout(15)
PubSubClient(*tls_)
setBufferSize(BuildConfig::BAMBU_MQTT_BUFFER_BYTES)
BAMBU_MQTT_KEEPALIVE_SEC = 30
random client ID prefix "bblp_"
direct mqtt_->connect(clientId, cloudUserId, accessToken)
no explicit tls_->connect in normal connectMqtt
Cloud reconnect 30s -> 60s after 5 fails -> 120s after 15 fails
initial pushall delay 2000ms
release PubSubClient + WiFiClientSecure on failure
watchdog disable/delete forbidden
setInsecure forbidden
```

- [ ] **Step 2: Push only the validator change and run CI**

Expected: `validate_bambu_pubsub_timeout_contract.py` FAILS for missing 6.12.0/main-loop process/reference markers while unrelated validators remain green.

- [ ] **Step 3: Confirm RED failure is semantic**

The failure must mention missing reference-runtime markers, not Python syntax/file errors.

---

### Task 2: Align the PlatformIO framework and BambuMqttService interface

**Files:**
- Modify: `platformio.ini`
- Modify: `src/network/BambuMqttService.h`
- Modify: `src/main.cpp`

**Interfaces:**
- Produces: `void BambuMqttService::process(uint32_t nowMs)` called once per Arduino loop iteration.

- [ ] **Step 1: Change the ESP32 platform**

```ini
platform = espressif32@6.12.0
```

Remove the project-level `-DMQTT_SOCKET_TIMEOUT=3` build flag. Keep all display and library settings unchanged.

- [ ] **Step 2: Replace task-owned service interface**

In `BambuMqttService.h`:

```cpp
bool begin(const BambuConfig& config, BambuConfigStore& store);
void process(uint32_t nowMs);
```

Remove `taskThunk`, `taskLoop` and `TaskHandle_t task_`. Keep snapshot/config mutex APIs.

- [ ] **Step 3: Call the service from main loop**

In `loop()` after `integrationConfigPortal.process()` and after obtaining `nowMs`:

```cpp
bambuMqttService.process(nowMs);
```

Keep input/tick/render order otherwise unchanged.

---

### Task 3: Port BambuHelper Cloud connection lifecycle

**Files:**
- Modify: `src/network/BambuMqttService.cpp`

**Interfaces:**
- Consumes: `BambuConfig`, `NetworkArbiter`, PubSubClient, WiFiClientSecure.
- Produces: loop-driven persistent Cloud MQTT session for only the active printer.

- [ ] **Step 1: Remove Stage-2/3/4 normal-path diagnostics**

Delete normal reconnect dependence on:

```cpp
BAMBU_REAL_TLS_TIMEOUT_MS
runLayeredConnectionProbe(...)
tls_->connect(...)
mqtt_real_tls_begin
mqtt_real_tls_ok
mqtt_real_tls_fail
```

Diagnostic heap logging may remain only where it does not open extra sockets.

- [ ] **Step 2: Add reference constants/state**

Use:

```cpp
constexpr uint16_t BAMBU_MQTT_PORT = 8883U;
constexpr uint16_t BAMBU_MQTT_KEEPALIVE_SEC = 30U;
constexpr uint32_t BAMBU_CLOUD_RECONNECT_BASE_MS = 30000U;
constexpr uint32_t BAMBU_CLOUD_RECONNECT_PHASE2_MS = 60000U;
constexpr uint32_t BAMBU_CLOUD_RECONNECT_PHASE3_MS = 120000U;
constexpr uint16_t BAMBU_BACKOFF_PHASE1_FAILS = 5U;
constexpr uint16_t BAMBU_BACKOFF_PHASE3_FAILS = 15U;
constexpr uint32_t BAMBU_PUSHALL_INITIAL_DELAY_MS = 2000U;
```

Add service state:

```cpp
uint16_t consecutiveFails_ = 0;
uint32_t connectTimeMs_ = 0;
bool initialPushallPending_ = false;
```

- [ ] **Step 3: Make `begin()` initialization-only**

Create the mutex, assign config/store/status and active instance, but do not create any FreeRTOS task.

- [ ] **Step 4: Implement `process(nowMs)`**

Move the previous task-loop decision tree into one non-looping method invocation:

```text
config revision changed -> disconnect/reset backoff/rejected-token latch
integration disabled -> disconnected state
invalid config -> unconfigured/token-invalid
rejected token -> token-invalid
Wi-Fi down -> network-error
not connected -> attempt reconnect only when backoff due
connected -> mqtt.loop()
connected + initial pushall pending + >=2s -> publish pushall once
```

No `for(;;)` and no `vTaskDelay()` inside the service.

- [ ] **Step 5: Rebuild clients exactly before reconnect**

`connectMqtt()` starts by destroying any existing PubSubClient/WiFiClientSecure. Under `NetworkArbiter`:

```cpp
tls_ = new WiFiClientSecure();
tls_->setCACertBundle(rootca_crt_bundle_start);
tls_->setTimeout(15);

mqtt_ = new PubSubClient(*tls_);
mqtt_->setServer(broker, 8883);
mqtt_->setCallback(mqttCallbackThunk);
mqtt_->setKeepAlive(30);
mqtt_->setBufferSize(40960);
```

Do not call `tls_->connect()` explicitly.

- [ ] **Step 6: Match Cloud client ID/connect pattern**

```cpp
snprintf(clientId, sizeof(clientId), "bblp_%08x%04x",
         (uint32_t)esp_random(), (uint16_t)(esp_random() & 0xFFFF));

esp_task_wdt_reset();
bool connected = mqtt_->connect(clientId,
                                config.cloudUserId.c_str(),
                                config.accessToken.c_str());
```

Never log username/token.

- [ ] **Step 7: Subscribe but delay pushall**

On successful MQTT CONNECT:

```cpp
mqtt_->subscribe(bambuReportTopic(active->serial).c_str());
connectTimeMs_ = millis();
initialPushallPending_ = true;
```

Do not publish pushall in `connectMqtt()`.

- [ ] **Step 8: Publish initial pushall from `process()` after 2 seconds**

Use the existing read-only payload and request topic. Call `esp_task_wdt_reset()` before publish, then clear `initialPushallPending_` only on successful publish.

- [ ] **Step 9: Implement BambuHelper-style failure backoff**

Reconnect interval helper:

```cpp
if (consecutiveFails_ >= 15) return 120000;
if (consecutiveFails_ >= 5) return 60000;
return 30000;
```

Increment on network/connect/subscribe failures. On rc 4/5, latch token rejection and do not churn. On successful connection clear `consecutiveFails_`.

- [ ] **Step 10: Keep callbacks and client release single-caller-safe**

Call `esp_task_wdt_reset()` at callback entry. `disconnectMqtt()` must disconnect/delete PubSubClient, stop/delete TLS client, clear connectivity and clear `initialPushallPending_`.

---

### Task 4: Update policy/docs/attribution contracts

**Files:**
- Modify: `AGENTS.md`
- Modify: `README.md`
- Modify: `docs/deployment.md`
- Modify: `docs/api-contract.md`
- Modify: `docs/hardware-acceptance.md`
- Modify: `THIRD_PARTY_NOTICES.md`

**Interfaces:**
- Produces: documentation matching the new loop-driven BambuHelper-aligned runtime.

- [ ] **Step 1: Remove obsolete single-real-TLS/3-second diagnostic claims**

Document the new direct PubSubClient-owned TLS path, 6.12.0 platform, loop-driven execution and 30/60/120-second reconnect backoff.

- [ ] **Step 2: Preserve security/read-only contracts**

Keep Manual Token, strict CA, only pushall publish, no control commands, no password/TFA and no secret logging.

- [ ] **Step 3: Strengthen attribution**

State that Cloud MQTT connection lifecycle, client recreation, random client ID, reconnect/backoff scheduling and delayed initial pushall are adapted from `Keralots/BambuHelper` under MIT.

---

### Task 5: Exact-head verification and artifact

**Files:**
- No production changes unless a verification failure requires a fix.

- [ ] **Step 1: Run exact-head CI**

Required checks:

```text
validate_tdisplay_setup.py
validate_provisioning_contract.py
validate_http_transport_contract.py
validate_app_shell_contract.py
validate_dashboard_apps_contract.py
validate_bambu_cloud_contract.py
validate_bambu_pubsub_timeout_contract.py
validate_bad_apple_contract.py
pio test -e native
prepare_bad_apple_asset.py
pio run -e lilygo-t-display-s3
manifest generation
artifact upload
```

- [ ] **Step 2: Verify both Linux/native and Windows/native jobs**

All tests must pass; ESP32-S3 compile/link must pass under `espressif32@6.12.0`.

- [ ] **Step 3: Download exact-head artifact and independently hash**

Verify ZIP digest and inner `firmware.bin` against CI manifest. Deliver application firmware only for `0x10000`; preserve NVS.

- [ ] **Step 4: Update PR #9 body**

Keep Draft. Record exact source SHA, CI run, artifact ID/hashes and required physical acceptance.

- [ ] **Step 5: Physical acceptance handoff to Codex**

Codex flashes exact firmware and monitors at least 10 minutes. Required evidence:

```text
no watchdog/panic/automatic reboot
MQTT connect and report data stable
transient connect failure recovers under backoff
A -> B -> A device switching works without reauth/reboot
old state does not cross-contaminate
Bambu screen does not whole-screen flash
Stock/Weather/HA remain usable
NVS survives reboot
no secret leak
```
