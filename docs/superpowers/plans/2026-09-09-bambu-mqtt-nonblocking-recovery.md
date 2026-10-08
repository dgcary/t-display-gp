# Bambu MQTT Nonblocking Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the observed ~120 s Bambu Cloud MQTT UI freeze and immediate reconnect loop while preserving persistent multi-printer read-only MQTT behavior.

**Architecture:** Restore a dedicated FreeRTOS Bambu MQTT worker so all blocking TCP/TLS/MQTT socket work is outside the Arduino UI loop. Keep the existing per-printer `MqttConn[4]` and `BambuState[4]` model. Bound both WiFiClientSecure TCP/socket connect and TLS handshake phases to 5 seconds, and anchor reconnect backoff to the time a failed connect returns.

**Tech Stack:** ESP32-S3, Arduino-ESP32 2.0.17, PlatformIO espressif32@6.12.0, FreeRTOS, WiFiClientSecure, PubSubClient 2.8.

**Spec:** Field evidence from 2026-09-09: repeated `mqtt_connect_fail rc=-2 elapsed_ms≈120000`, UI/portal blocked during connect, and immediate reconnect despite logged 30/60 s retry.

## Global Constraints

- Bambu Cloud remains Manual Token only and read-only.
- Keep strict CA validation; `setInsecure()` remains forbidden.
- Keep up to four persistent configured MQTT slots and two direct-navigation Bambu pages.
- Keep MQTT keepalive at 30 s and buffer at 40960 bytes.
- Do not erase or migrate NVS for this fix.
- Do not weaken Wi-Fi/TLS security or change Bambu credentials.

---

### Task 1: Reproduce the blocking contract

**Files:**
- Modify: `tools/validate_bambu_pubsub_timeout_contract.py`

**Interfaces:**
- Consumes: current `BambuMqttService` source and `main.cpp`.
- Produces: a static RED gate proving the old architecture violates the new nonblocking/timeout/backoff contract.

- [x] **Step 1: Write the failing contract**

Require a `bambu-mqtt` FreeRTOS worker, forbid `bambuMqttService.process(nowMs)` in the Arduino loop, require 5 s TCP and TLS handshake timeouts, and require the retry anchor to be recorded with `millis()` after failure completion rather than `nowMs` before connect.

- [x] **Step 2: Run CI and verify RED**

Expected failures include all three root-cause classes above. Run `34314846594` confirmed RED at `validate_bambu_pubsub_timeout_contract.py`.

### Task 2: Move MQTT socket work off the Arduino loop

**Files:**
- Modify: `src/network/BambuMqttService.h`
- Modify: `src/network/BambuMqttService.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: existing `process(uint32_t)` per-slot service logic.
- Produces: `taskThunk(void*)`, `taskLoop()`, `TaskHandle_t task_`; `process(uint32_t)` becomes worker-private and is no longer called by `loop()`.

- [ ] **Step 1: Start worker in `begin()`**

Use:

```cpp
xTaskCreatePinnedToCore(taskThunk, "bambu-mqtt", 8192, this, 1, &task_, 0);
```

Rollback `activeInstance_`, mutex, and `task_` if creation fails.

- [ ] **Step 2: Run existing multi-printer service loop in worker**

```cpp
void BambuMqttService::taskThunk(void* arg) {
  static_cast<BambuMqttService*>(arg)->taskLoop();
}

void BambuMqttService::taskLoop() {
  for (;;) {
    process(millis());
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
```

- [ ] **Step 3: Remove synchronous main-loop call**

Delete only:

```cpp
bambuMqttService.process(nowMs);
```

Portal/UI processing remains on the Arduino loop.

### Task 3: Bound connect phases and fix retry timing

**Files:**
- Modify: `include/build_config.h`
- Modify: `src/network/BambuMqttService.cpp`

**Interfaces:**
- Produces: `BuildConfig::BAMBU_MQTT_CONNECT_TIMEOUT_SEC = 5U` and completion-anchored `lastMqttAttemptMs`.

- [ ] **Step 1: Bound TCP/socket and TLS handshake separately**

```cpp
conn.tls->setTimeout(BuildConfig::BAMBU_MQTT_CONNECT_TIMEOUT_SEC);
conn.tls->setHandshakeTimeout(BuildConfig::BAMBU_MQTT_CONNECT_TIMEOUT_SEC);
```

This prevents fallback to Arduino-ESP32's ~120 s TLS handshake default.

- [ ] **Step 2: Anchor backoff after failure completion**

Do not assign `conn.lastMqttAttemptMs = nowMs` before `mqtt->connect`. On every retryable failure path, assign:

```cpp
conn.lastMqttAttemptMs = millis();
conn.mqttAttempted = true;
```

before returning. The next 30/60/120 s interval therefore starts after the blocking call has returned.

- [ ] **Step 3: Preserve auth latch semantics**

MQTT rc 4/5 still sets `tokenRejected` and does not spin reconnects until config revision changes.

### Task 4: Update stale contracts/docs and verify exact head

**Files:**
- Modify: `tools/validate_bambu_cloud_contract.py`
- Modify: `AGENTS.md`
- Modify: `docs/deployment.md`
- Modify: `docs/hardware-acceptance.md`

**Interfaces:**
- Produces: documentation and validators matching the recovered worker architecture.

- [ ] **Step 1: Remove stale no-task assertions**

Require the worker and bounded timeouts; continue forbidding insecure TLS, login flows, printer-control publishes, WDT disabling, and single-active-printer runtime state.

- [ ] **Step 2: Add physical acceptance gates**

Verify while Bambu Cloud is unreachable: buttons and local port 8081 remain responsive, each failed connect is far below 120 s, and the next connect begins only after the logged retry interval.

- [ ] **Step 3: Run exact-head CI**

Required: all validators PASS, Linux native PASS, Windows native PASS, Bad Apple asset roundtrip PASS, ESP32-S3 PlatformIO build PASS, artifact manifest generated.

- [ ] **Step 4: Deliver exact-head firmware**

Download artifact, independently verify ZIP and `firmware.bin` SHA256, and flash only application firmware at `0x10000` preserving NVS/bootloader/partitions.
