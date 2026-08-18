# Market Stability + Landscape UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make market-data refresh resilient to EastMoney intraday instability while preserving quote responsiveness, and redesign the T-Display-S3 UI to 320×170 landscape with a today-open reference line.

**Architecture:** Keep one `MarketDataWorker` HTTP execution task, but add request diagnostics, quote-priority scheduling, latest-wins intraday work, bounded deferred retries, and separate quote/intraday health. The UI remains cache-driven and non-blocking. Device rotation changes to landscape and the chart range includes previous close plus today open.

**Tech Stack:** ESP32-S3, Arduino/C++17, PlatformIO, FreeRTOS queue/task APIs, Arduino `HTTPClient`, `WiFiClientSecure`, TFT_eSPI, U8g2_for_TFT_eSPI, Unity native tests.

**Spec:** `docs/superpowers/specs/2026-08-18-market-stability-landscape-design.md`

## Global Constraints

- Base implementation branch is `feature/market-stability-landscape`, created from `f7ec12d42e875651719a49334259570017c6e3fd`.
- Main loop never performs blocking market HTTP.
- HTTP remains inside `MarketDataWorker`.
- EastMoney remains V1 quote + intraday primary; Tencent remains quote fallback only.
- Provider/network failures preserve last valid quote and intraday cache.
- Parser validation is not weakened.
- No infinite retry, no unbounded timeout extension, no TLS-security downgrade.
- Keep `HTTPClient::setReuse(false)` in this change.
- Intraday retry is deferred and bounded to 3 total attempts: ~1500 ms then ~4000 ms, each ±20% jitter.
- QUOTE/PRIMARY_PROBE request TTL is 15000 ms; INTRADAY request/retry TTL is 10000 ms.
- Quote delay threshold is 15 s; intraday delay threshold is 180 s.
- Physical display becomes 320×170 landscape.

---

### Task 1: HTTP Request Diagnostics

**Files:**
- Modify: `src/network/HttpTransport.h`
- Modify: `src/network/HttpTransport.cpp`
- Modify: `src/network/MarketDataWorker.h`
- Test: `test/test_live_providers/test_main.cpp`
- Test: `test/test_market_data_worker/test_main.cpp`

**Interfaces:**
- Produces: `HttpResponse` diagnostics fields for status, native HTTPClient error, TLS error, expected/received bytes, and elapsed milliseconds.
- Produces: `MarketResult` request metadata needed for one-line serial completion logs.

- [ ] **Step 1: Add failing transport/result diagnostics tests**

Add assertions equivalent to:

```cpp
HttpResponse response;
response.error = HttpTransportError::NETWORK;
response.statusCode = 0;
response.nativeError = -5;
response.tlsError = -0x7280;
response.expectedBytes = 13824;
response.receivedBytes = 8192;
response.elapsedMs = 2700;
TEST_ASSERT_EQUAL(-5, response.nativeError);
TEST_ASSERT_EQUAL_UINT32(8192, response.receivedBytes);
```

Also verify a `MarketResult` can carry request/provider/attempt/queue-wait/transport diagnostics without changing parsed quote/intraday objects.

- [ ] **Step 2: Run native tests and verify RED**

Run:

```bash
pio test -e native -f test_live_providers -f test_market_data_worker
```

Expected: compile/test failure because the diagnostics fields do not exist.

- [ ] **Step 3: Implement diagnostics without changing Provider semantics**

Extend transport/result models with explicit fields. `HttpTransport::get()` must measure elapsed time with `millis()`, preserve `HTTPClient::GET()` negative error codes, call `WiFiClientSecure::lastError()` before the client is destroyed when a transport failure occurs, and record byte counts/content length. Keep `setReuse(false)` and existing size checks.

- [ ] **Step 4: Run affected tests GREEN**

```bash
pio test -e native -f test_live_providers -f test_market_data_worker
```

- [ ] **Step 5: Commit**

```bash
git add src/network/HttpTransport.* src/network/MarketDataWorker.h test/test_live_providers/test_main.cpp test/test_market_data_worker/test_main.cpp
git commit -m "feat: add market request diagnostics"
```

---

### Task 2: Separate Quote and Intraday Health

**Files:**
- Modify: `src/app/StockController.h`
- Modify: `src/app/StockController.cpp`
- Test: `test/test_stock_controller/test_main.cpp`
- Test: `test/test_acceptance_behavior/test_main.cpp`

**Interfaces:**
- Produces: independent quote/intraday health state with last error, last attempt, last success, consecutive failures.
- Produces: `StockViewModel::quoteAgeSeconds`, `intradayAgeSeconds`, `quoteDelayed`, `intradayDelayed`.

- [ ] **Step 1: Add failing health-isolation tests**

Required tests:

```text
INTRADAY fail -> quote health remains healthy and cached quote remains visible.
QUOTE success -> does not clear existing intraday error.
QUOTE fail -> intraday success does not clear quote error.
Single intraday failure with an existing chart -> no generic page-level data error.
Quote age >= 15 s -> quoteDelayed=true.
Intraday age >= 180 s -> intradayDelayed=true.
```

- [ ] **Step 2: Run tests RED**

```bash
pio test -e native -f test_stock_controller -f test_acceptance_behavior
```

- [ ] **Step 3: Implement independent health state**

Replace shared `StockCacheEntry::lastError` behavior with two channel-health objects. Failed requests update only their channel. Successful requests clear only their own channel and update `lastSuccessMs`. Existing cached payloads are never cleared on failure.

UI-facing badge policy at Controller level:

```text
Wi-Fi offline -> 离线
no quote yet -> 等待报价
quoteDelayed -> 报价延迟
intradayDelayed -> 分时延迟
otherwise -> empty
```

An isolated intraday failure with a still-fresh chart does not set `分时延迟`.

- [ ] **Step 4: Run tests GREEN**

```bash
pio test -e native -f test_stock_controller -f test_acceptance_behavior
```

- [ ] **Step 5: Commit**

```bash
git add src/app/StockController.* test/test_stock_controller/test_main.cpp test/test_acceptance_behavior/test_main.cpp
git commit -m "feat: separate quote and intraday health"
```

---

### Task 3: Quote-Priority Worker QoS and Bounded Intraday Retry

**Files:**
- Modify: `src/network/MarketDataWorker.h`
- Modify: `src/network/MarketDataWorker.cpp`
- Modify: `src/app/StockController.h`
- Modify: `src/app/StockController.cpp`
- Modify: `include/build_config.h`
- Test: `test/test_market_data_worker/test_main.cpp`
- Test: `test/test_stock_controller/test_main.cpp`
- Test: `test/test_acceptance_behavior/test_main.cpp`
- Create: `test/test_market_request_policy/test_main.cpp`

**Interfaces:**
- `MarketRequest` adds `createdMs`, `notBeforeMs`, `attempt`, `priority`/equivalent scheduling metadata.
- Worker guarantees quote-class work is selected before waiting intraday work.
- Waiting intraday is latest-wins and at most one low-priority pending item is retained.
- Every accepted request eventually yields success/failure/cancelled-expired result so Controller can release outstanding state.

- [ ] **Step 1: Add failing scheduler/retry policy tests**

Cover:

```text
current QUOTE chosen before waiting INTRADAY;
new current-stock INTRADAY replaces older not-started intraday request;
expired QUOTE (>15 s) and expired INTRADAY (>10 s) do not execute HTTP;
NETWORK / connection-lost / read-timeout / HTTP 408 / HTTP 5xx are retryable;
PARSE / MISSING_FIELD / BODY_TOO_LARGE / 4xx except 408 are not immediate-retryable;
max intraday attempts = 3;
retry #2 due ~=1500 ms ±20%; retry #3 due ~=4000 ms ±20%;
quote work can run between deferred intraday retries;
queue/result pressure cannot leave Controller outstanding forever.
```

- [ ] **Step 2: Run tests RED**

```bash
pio test -e native -f test_market_data_worker -f test_market_request_policy -f test_stock_controller -f test_acceptance_behavior
```

- [ ] **Step 3: Implement QoS scheduler**

Keep one HTTP worker task. Replace simple FIFO execution semantics with bounded quote-class pending plus one latest-wins intraday slot/deferred retry. Do not run retry loops inside Provider methods. Retry requests carry `notBeforeMs`; worker always services ready quote traffic first.

Use constants in `build_config.h`:

```cpp
constexpr uint32_t QUOTE_REQUEST_TTL_MS = 15000;
constexpr uint32_t INTRADAY_REQUEST_TTL_MS = 10000;
constexpr uint8_t INTRADAY_MAX_ATTEMPTS = 3;
constexpr uint32_t INTRADAY_RETRY_1_MS = 1500;
constexpr uint32_t INTRADAY_RETRY_2_MS = 4000;
```

Jitter is deterministic/testable through a small helper or injectable seed and must remain within ±20%.

- [ ] **Step 4: Add concise serial request completion logging**

One completed request produces one line containing at least:

```text
[md] id=... type=QUOTE|INTRADAY|PROBE symbol=... provider=EM|TX attempt=1/3 queue=...ms dur=...ms http=... native=... tls=... bytes=.../... result=...
```

Do not log full response bodies.

- [ ] **Step 5: Run scheduler/controller tests GREEN**

```bash
pio test -e native -f test_market_data_worker -f test_market_request_policy -f test_stock_controller -f test_acceptance_behavior
```

- [ ] **Step 6: Commit**

```bash
git add src/network/MarketDataWorker.* src/app/StockController.* include/build_config.h test/test_market_data_worker test/test_market_request_policy test/test_stock_controller test/test_acceptance_behavior
git commit -m "feat: prioritize quotes and bound intraday retries"
```

---

### Task 4: 320×170 Landscape UI and Today-Open Reference

**Files:**
- Modify: `src/device/DeviceLayer.cpp`
- Modify: `src/ui/StockScreen.h`
- Modify: `src/ui/StockScreen.cpp`
- Test: `test/test_stock_screen/test_main.cpp`
- Modify: `tools/validate_tdisplay_setup.py` only if its rotation/layout contract requires update.

**Interfaces:**
- Device logical display is 320×170 landscape.
- `StockScreenMath::chartRange(series, prevClose, open)` includes valid previous-close and open references.
- Invalid `open <= 0` is ignored.

- [ ] **Step 1: Add failing landscape math tests**

Required assertions:

```text
all layout rectangles are inside x=0..319 and y=0..169;
chartX maps trading minutes inside the right chart panel;
chartY is inside chart bounds;
chartRange includes intraday min/max, prevClose and valid open;
open=0 does not expand range;
lunch discontinuity remains detectable;
UTF-8 truncation still works.
```

- [ ] **Step 2: Run stock-screen tests RED**

```bash
pio test -e native -f test_stock_screen
```

- [ ] **Step 3: Implement landscape rotation and layout**

Use the landscape TFT rotation that yields `display.width()==320` and `display.height()==170` on T-Display-S3. Layout target:

```text
0..115   left quote/info panel
116..319 right chart panel
compact footer/status inside left/bottom areas without reducing chart below ~145 px height
```

Keep red-up/green-down behavior and cached rendering.

- [ ] **Step 4: Add chart references**

Draw distinct non-color-only reference styles:

```text
昨收: dashed reference
今开: different dash pattern and/or short 今开 label
```

Both use the same chart scale. Omit today-open line when open is invalid.

- [ ] **Step 5: Run stock-screen tests GREEN**

```bash
pio test -e native -f test_stock_screen
```

- [ ] **Step 6: Commit**

```bash
git add src/device/DeviceLayer.cpp src/ui/StockScreen.* test/test_stock_screen tools/validate_tdisplay_setup.py
git commit -m "feat: add landscape market display"
```

---

### Task 5: Full Integration, Documentation and Acceptance Gate

**Files:**
- Modify: `README.md`
- Modify: `AGENTS.md` if landscape/instrumentation invariants need to be explicit.
- Modify: `docs/api-contract.md`
- Modify: `docs/deployment.md`
- Modify: `docs/hardware-acceptance.md`
- Modify: `.github/workflows/ci.yml` if new validation/test directories require explicit steps.

**Interfaces:**
- Documentation records 320×170 landscape, request diagnostics, retry policy, health semantics and stability acceptance.

- [ ] **Step 1: Run complete automated regression**

```bash
pio test -e native
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
pio run -e lilygo-t-display-s3
```

Expected: all commands PASS before documentation/PR readiness is claimed.

- [ ] **Step 2: Review serial/logging and cache behavior against spec**

Confirm code paths guarantee:

```text
main loop contains no market HTTP;
failed request does not clear quote/chart;
quote success cannot clear intraday health;
intraday success cannot clear quote health;
worker has bounded low-priority intraday pending;
no retry loop can occupy worker for all three attempts;
```

- [ ] **Step 3: Update docs**

Record exact behavior and the physical validation checklist. Do not mark physical items PASS without hardware evidence.

- [ ] **Step 4: Run final regression again after docs/CI edits**

```bash
pio test -e native
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
pio run -e lilygo-t-display-s3
```

- [ ] **Step 5: Commit**

```bash
git add README.md AGENTS.md docs .github/workflows/ci.yml
git commit -m "docs: document market stability acceptance"
```

- [ ] **Step 6: Hardware acceptance remains pending until board test**

Required physical run after PR code is available:

```text
5 stocks configured;
100+ button switches;
500+ quote attempts, target >=99% success on test network;
30+ intraday refresh cycles, target >=80% cycle success after bounded retries;
quote P95 interval <=7 s when healthy;
single intraday failure does not produce quote gap >10 s;
intraday P95 age <=180 s;
zero watchdog reset/unexpected reboot/cache loss;
final target 09:25–15:10 full trading day.
```
