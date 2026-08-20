# Home Assistant + Crypto Suite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the existing T-Display GP multi-app shell with a secure read-only Home Assistant dashboard and a low-frequency Crypto market dashboard, keeping Stock/Weather/Nixie/DeviceInfo behavior intact.

**Architecture:** Both remote apps reuse the single shared `AppDataWorker`; no new FreeRTOS worker is created. `AppDataWorker` gains typed result queues so late results for Weather/Home Assistant/Crypto cannot be consumed and discarded by another app. Crypto uses the existing public `HttpTransport`/`NetworkArbiter`; Home Assistant uses a dedicated strict-TLS transport with a configured CA certificate and Bearer token, never `setInsecure()`.

**Tech Stack:** Arduino/C++17, PlatformIO, ESP32-S3, TFT_eSPI, U8g2_for_TFT_eSPI, ArduinoJson 6.21.6, Home Assistant REST API, CoinGecko keyless public REST API.

**Spec:** This plan is the implementation spec for the stacked feature branch based on `feature/nixie-clock`.

## Global Constraints

- Hardware remains LILYGO T-Display-S3, 320x170 landscape rotation 3.
- Startup remains StockApp.
- Existing GPIO0/GPIO14 short/long semantics remain unchanged.
- No additional FreeRTOS worker per app.
- At most one external HTTP/TLS operation at once through `NetworkArbiter`.
- Existing market transport timeouts and TLS diagnostics remain unchanged.
- Home Assistant credentials must never be logged or returned by `/api/status`.
- Home Assistant V1 is read-only and requires HTTPS plus a configured CA certificate; no `setInsecure()` with Bearer credentials.
- Crypto V1 displays Bitcoin, Ethereum and Solana in USD with 24h change; refresh default 60 seconds.
- Home Assistant V1 supports 1-4 configured entity IDs with optional display labels; refresh default 30 seconds.
- Both network apps preserve their last valid cache on failure and do not schedule while inactive.
- PR remains Draft until physical T-Display-S3 acceptance.

---

### Task 1: Typed shared app-data routing

**Files:**
- Modify: `src/network/AppDataTypes.h`
- Modify: `src/network/AppDataWorker.h`
- Modify: `src/network/AppDataWorker.cpp`
- Modify: `src/app/WeatherController.cpp`
- Modify: `test/test_weather_controller/test_main.cpp`

**Interfaces:**
- `IAppDataQueue::tryReceive(AppDataRequestType type, AppDataResult& result)` returns only results for the requested app-data type.
- One worker task executes all Weather/Home Assistant/Crypto requests.

- [ ] Write failing routing contract/tests.
- [ ] Verify RED.
- [ ] Implement per-type result queues without adding workers.
- [ ] Update WeatherController and its fake queue.
- [ ] Verify existing Weather behavior stays GREEN.

### Task 2: Configuration schema v3 and secure provisioning

**Files:**
- Modify: `lib/core/AppConfig.h`
- Modify: `lib/core/AppConfig.cpp`
- Modify: `src/network/ProvisioningForm.h`
- Modify: `src/network/ProvisioningForm.cpp`
- Modify: `src/network/ProvisioningService.cpp`
- Modify tests: `test/test_app_config/test_main.cpp`, `test/test_provisioning_form/test_main.cpp`

**Interfaces:**
- `HomeAssistantConfig`: enabled, HTTPS base URL, 30-300 s refresh, bearer token, CA PEM, 1-4 entity configs.
- Existing schema v1/v2 decodes migrate to schema v3 with HA disabled.
- `/api/status` returns a redacted config view: never token/CA contents.
- LAN web form uses password/textarea inputs and preserves existing secrets when left blank.

- [ ] Add failing schema/security tests.
- [ ] Verify RED.
- [ ] Implement v3 encode/decode/validation/migration.
- [ ] Extend provisioning validation and web UI.
- [ ] Add redacted status encoding.
- [ ] Verify secrets never appear in status JSON.

### Task 3: Crypto provider/controller/UI

**Files:**
- Create: `src/network/CryptoProvider.h/.cpp`
- Create: `src/app/CryptoController.h/.cpp`
- Create: `src/app/CryptoApp.h/.cpp`
- Create: `src/ui/CryptoScreen.h/.cpp`
- Modify: `src/network/AppDataTypes.h`
- Modify: `src/network/AppDataWorker.cpp`
- Modify: `src/app/AppShell.h`
- Modify: `src/main.cpp`
- Create tests: `test/test_crypto_provider/test_main.cpp`, `test/test_crypto_controller/test_main.cpp`

**Interfaces:**
- One CoinGecko request fetches `bitcoin,ethereum,solana` USD price, 24h change and last update.
- Provider parses fail-closed into bounded `CryptoSnapshot`.
- Controller refreshes only while active+online, default every 60 s, preserves cache on error.

- [ ] Write provider/controller failing tests.
- [ ] Verify RED.
- [ ] Implement strict bounded parser/provider.
- [ ] Implement active-only controller and three-row UI.
- [ ] Register `AppId::CRYPTO` and menu entry `加密货币`.
- [ ] Verify GREEN.

### Task 4: Home Assistant secure read-only dashboard

**Files:**
- Create: `src/network/HomeAssistantProvider.h/.cpp`
- Create: `src/app/HomeAssistantController.h/.cpp`
- Create: `src/app/HomeAssistantApp.h/.cpp`
- Create: `src/ui/HomeAssistantScreen.h/.cpp`
- Modify: `src/network/AppDataTypes.h`
- Modify: `src/network/AppDataWorker.cpp`
- Modify: `src/app/AppShell.h`
- Modify: `src/main.cpp`
- Create tests: `test/test_home_assistant_provider/test_main.cpp`, `test/test_home_assistant_controller/test_main.cpp`

**Interfaces:**
- Fetches `GET <base_url>/api/states/<entity_id>` with `Authorization: Bearer <token>`.
- TLS uses configured CA through `WiFiClientSecure::setCACert`; no insecure mode.
- One entity request at a time; controller walks 1-4 entities sequentially and keeps per-entity last valid values.
- No write/service endpoints in V1.

- [ ] Write provider/controller failing tests.
- [ ] Verify RED.
- [ ] Implement secure transport/provider with bounded body.
- [ ] Implement sequential active-only controller and compact state UI.
- [ ] Register `AppId::HOME_ASSISTANT` and menu entry `智能家居`.
- [ ] Verify no token is logged or included in status responses.

### Task 5: Contracts, docs, full verification and artifact

**Files:**
- Create: `tools/validate_dashboard_apps_contract.py`
- Modify: `.github/workflows/ci.yml`
- Modify: `AGENTS.md`
- Modify: `README.md`
- Modify: `docs/api-contract.md`
- Modify: `docs/deployment.md`
- Modify: `docs/hardware-acceptance.md`

- [ ] Add contract checks for app registration, shared-worker usage, active-only scheduling, CoinGecko endpoint, HA strict TLS/redaction.
- [ ] Run all validators and native tests.
- [ ] Run `pio run -e lilygo-t-display-s3`.
- [ ] Verify RAM/Flash remain within board limits.
- [ ] Verify exact-head GitHub Actions Ubuntu/Windows jobs.
- [ ] Download artifact, recompute ZIP/firmware/partition/bootloader SHA256, and compare with manifest.
- [ ] Keep PR Draft and prepare one Codex flash/physical acceptance instruction for the combined firmware.
