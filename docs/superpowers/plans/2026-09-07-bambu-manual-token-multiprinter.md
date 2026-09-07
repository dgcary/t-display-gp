# Bambu Manual Token + Multi-Printer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace device-side Bambu account/SMS login with manual token setup, persist up to four printers locally, and support active-printer MQTT switching from :8081.

**Architecture:** BambuConfig schema v2 stores region, token, derived userId, four printer slots, and active selection. Normal boot/switching uses only persisted identity + Cloud MQTT; Cloud HTTPS printer discovery is explicit user action only. Legacy schema v1 is migrated in memory.

**Tech Stack:** Arduino/C++17, ESP32-S3, ArduinoJson 6.21.6, PubSubClient 2.8, PlatformIO, Unity native tests, Python static validators.

**Spec:** `docs/superpowers/specs/2026-09-07-bambu-manual-token-multiprinter-design.md`

## Global Constraints

- Target remains `lilygo-t-display-s3` / ESP32-S3 / 320x170 landscape.
- Bambu MQTT remains Cloud-only, read-only, CA-verified, port 8883, buffer 40960.
- No `setInsecure()` in Bambu credentialed HTTPS or MQTT.
- No account password, OTP, TFA key, or raw token may be logged or returned by status APIs.
- Normal printer switching must not invoke Cloud HTTPS discovery.
- Final delivery must use exact-head CI artifact; normal flash is only `firmware.bin` at `0x10000` preserving NVS.

---

### Task 1: Config schema v2 + migration

**Files:**
- Modify: `src/network/BambuConfig.h`
- Modify: `src/network/BambuConfig.cpp`
- Modify: `test/test_bambu_config/test_main.cpp`

**Interfaces:**
- Produces `BambuPrinterConfig { std::string serial; std::string name; }`
- Produces bounded `printers` array/count and `activePrinterIndex` access helpers.

- [ ] Add RED tests: enabled config requires token + at least one valid printer; two printers round-trip; schema-v1 token/single-printer migrates to schema-v2; duplicate/unsafe serial fails.
- [ ] Run native config tests and confirm RED.
- [ ] Implement schema v2 encode/decode + v1 migration and validation.
- [ ] Run config tests and confirm GREEN.

### Task 2: Portal model for token + local printer list

**Files:**
- Modify: `src/network/BambuPortalModel.h`
- Modify: `src/network/BambuPortalModel.cpp`
- Modify: `test/test_bambu_portal_model/test_main.cpp`

**Interfaces:**
- Input carries region, optional replacement token, four printer rows, active serial/index.
- Blank token preserves existing token/userId.
- New token clears old userId until local JWT derivation succeeds.

- [ ] Add RED tests for blank-token preservation, replacing token invalidates old userId, multi-printer replacement, active selection, clear credentials.
- [ ] Run portal-model tests and confirm RED.
- [ ] Implement minimal merge/status helpers.
- [ ] Run portal-model tests and confirm GREEN.

### Task 3: Simplify MQTT service identity/switching

**Files:**
- Modify: `src/network/BambuMqttService.h`
- Modify: `src/network/BambuMqttService.cpp`
- Modify: `src/network/BambuSessionModel.*` only if obsolete relogin states must be retired.
- Test: native/static contract tests.

**Interfaces:**
- Service reads active printer from config snapshot.
- Config revision change disconnects MQTT, resets visible Bambu state, and reconnects to active serial.
- No automatic password relogin/challenge loop in the primary path.

- [ ] Add RED static/session assertions that normal token mode has no pending verification requirement and switching resets old printer state.
- [ ] Confirm RED.
- [ ] Remove account-login/challenge dependency from normal task loop; use persisted token/userId and selected printer.
- [ ] Ensure revision switch clears old `BambuState` before reconnect.
- [ ] Confirm GREEN.

### Task 4: :8081 manual token + multi-printer UI/API

**Files:**
- Modify: `src/network/IntegrationConfigPortal.cpp`
- Modify: `src/network/IntegrationConfigPortal.h` if BambuCloudClient dependency can be reduced/removed.
- Modify: `tools/validate_bambu_cloud_contract.py`

**Interfaces:**
- `POST /api/bambu/config`: region, optional token, printer rows, active printer.
- `POST /api/bambu/discover`: explicit token-authenticated discovery only.
- `GET /api/bambu/printers`: local saved/discovered cache only; never implicit Cloud request.
- Existing `/api/bambu/status` remains secret-safe.

- [ ] Change validator first to require manual token fields/local printer list and forbid primary account/password/verification UI/routes.
- [ ] Run validator and confirm RED.
- [ ] Rewrite Bambu portal UI to token + four printer rows + active selector.
- [ ] On save, derive `cloudUserId` with `extractBambuUserIdFromJwt`; profile fallback only if needed during explicit save/test.
- [ ] Make discovery explicit and non-destructive on failure.
- [ ] Run validator and confirm GREEN.

### Task 5: Docs + full exact-head verification

**Files:**
- Modify: `AGENTS.md`, `README.md`, `docs/api-contract.md`, `docs/deployment.md`, `docs/hardware-acceptance.md`
- Update: Draft PR #9 body.

- [ ] Align docs with manual token + local multi-printer contract.
- [ ] Run all Python validators.
- [ ] Run `pio test -e native`.
- [ ] Run Bad Apple asset verification.
- [ ] Run `pio run -e lilygo-t-display-s3`.
- [ ] Push exact final HEAD and verify GitHub Actions all-green.
- [ ] Download exact-head artifact, verify manifest/source SHA/firmware SHA256/offset `0x10000`.
- [ ] Deliver only the exact-head `firmware.bin` to the user.
