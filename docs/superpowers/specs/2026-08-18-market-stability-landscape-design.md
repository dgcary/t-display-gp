# T-Display GP Market Stability + Landscape UI Design

**Date:** 2026-08-18

**Base commit:** `f7ec12d42e875651719a49334259570017c6e3fd`

## Goal

Improve live-market stability without blocking the UI, preserve the last valid screen during provider failures, and redesign the T-Display-S3 screen to 320×170 landscape with a clearer intraday chart and an opening-price reference line.

## Non-negotiable constraints

- Main loop never performs blocking market HTTP.
- HTTP remains inside `MarketDataWorker`.
- UI/Controller depend on Provider abstractions, not raw provider payloads.
- EastMoney remains V1 primary for quote + intraday.
- Tencent remains V1 quote fallback only.
- Provider/network failures never clear the last valid quote or intraday cache.
- Parser validation remains strict.
- No infinite retry, no unbounded timeout increase, no TLS-security weakening as a stability workaround.
- Request/response behavior changes are test-first.
- Button/UI responsiveness must not regress.

## 1. Request scheduling architecture

Keep one market worker task and one active HTTP execution path to avoid concurrent TLS heap pressure, but replace the current simple FIFO semantics with request QoS.

Priority order:

1. Current-stock quote.
2. Background quote for one non-current stock.
3. EastMoney primary recovery probe.
4. Current-stock intraday.
5. Intraday retry.

The worker must never accumulate multiple waiting intraday requests for stocks the user has already left. Waiting intraday uses **latest-wins** semantics: at most one pending intraday request is retained, and a newer current-stock intraday request replaces an older not-yet-started one.

Each request carries creation time, attempt count and a bounded deadline/TTL. Expired low-value work is dropped before HTTP execution rather than consuming network time.

An already-running synchronous HTTP request is not force-cancelled. Its damage is bounded by transport timeouts; once it completes/fails, quote traffic is serviced before any deferred intraday retry.

## 2. Retry policy

Only transient transport/server failures are eligible for immediate intraday retry:

- connection failure/lost connection;
- read timeout;
- truncated body;
- HTTP 408;
- HTTP 5xx.

No immediate retry for parser/schema failures, unsupported data, body-too-large, or normal HTTP 4xx.

Maximum intraday attempts per refresh cycle: **3 total**.

Retry delay:

- retry #1: ~1500 ms plus bounded ±20% jitter;
- retry #2: ~4000 ms plus bounded ±20% jitter;
- then stop the cycle and retain the previous intraday cache.

Retries are deferred work, not loops inside `EastMoneyProvider::fetchIntraday()`, so quote requests can run between attempts.

## 3. HTTP diagnostics

Extend the transport result so each completed request can report:

- request ID;
- request type;
- symbol;
- provider;
- attempt number;
- queue wait time;
- request duration;
- HTTP status;
- HTTPClient native error code;
- WiFiClientSecure/TLS last error when available;
- expected Content-Length;
- actual received bytes;
- transport error;
- provider/parser error.

Serial logging is one concise completion line per request. Diagnostics must distinguish failures before HTTP status from failures while reading a HTTP 200 response.

Do not change connection reuse in the first implementation: retain `setReuse(false)` until logs provide evidence that new-connection cost is a primary failure source.

Keep current TLS behavior unchanged in this stability change; TLS certificate verification hardening is a separate future security task.

## 4. Quote and intraday health separation

Replace shared `StockCacheEntry::lastError` semantics with independent quote and intraday health state.

Each channel records at minimum:

- last error;
- last attempt time;
- last success time;
- consecutive failure count.

Quote success clears only quote error state. Intraday success clears only intraday error state.

Existing valid `QuoteSnapshot` and `IntradaySeries` remain untouched when the corresponding request fails.

The ViewModel exposes quote age and intraday age separately.

## 5. Error display policy

UI no longer turns a single failed intraday refresh into the generic page-level `数据异常` state.

Policy:

- Wi-Fi disconnected: `离线`.
- Quote has no valid data yet: `等待报价`.
- Quote cache exists but quote health is degraded/stale: `报价延迟`.
- One isolated intraday failure with an existing chart: no prominent error badge; retry silently.
- Intraday remains stale after repeated failure / exceeds the chosen stale threshold: `分时延迟`.

A successful quote must not clear an intraday-delay state, and a successful intraday must not clear quote-delay state.

## 6. Result delivery reliability

The worker must not silently lose a result in a way that leaves `StockController::outstanding_` permanently occupied.

Design requirement: every accepted request must eventually produce either a normal result/failure result or an explicit cancellation/expiry result that lets the Controller release outstanding state.

Queue-full behavior must be bounded and testable.

## 7. Landscape UI

Physical panel remains ST7789 170×320, but application rotation changes from portrait `setRotation(0)` to a landscape rotation producing **320×170** logical coordinates.

Layout:

- Left information panel: approximately 116 px wide.
- Right chart panel: remaining approximately 204 px.
- Bottom/footer is compact and must not materially reduce chart height.

Left panel contains:

- stock name/code;
- large current price;
- change and change percent;
- compact open/high/low/previous-close values;
- compact volume/amount;
- page position and Wi-Fi/provider/error status.

Right panel contains the intraday chart and keeps the lunch-break discontinuity.

The landscape redesign should prioritize readable price + larger chart rather than preserving the old portrait spacing.

## 8. Intraday reference lines

The landscape chart shows two distinct reference levels when valid:

1. **Previous close** — existing reference line.
2. **Today open** — new opening-price reference line based on `QuoteSnapshot::open`.

Both lines use the same chart Y-axis scaling as the price series and must not alter the price data.

The two references must be visually distinguishable by line pattern/label without relying only on color. Small labels such as `昨收` and `今开` may be used when space permits.

Chart-range calculation must include price series, previous close and open price so neither reference line is clipped when it lies outside the current intraday point range.

If opening price is zero/invalid, the opening reference is omitted rather than guessed.

## 9. Test strategy

Test-first coverage must include:

- intraday failure does not poison quote health;
- quote success does not clear intraday error;
- intraday success does not clear quote error;
- cached quote/chart survive failure;
- quote priority over waiting intraday;
- latest-wins intraday pending behavior;
- stale low-priority request expiry;
- retry classification and maximum attempts;
- retry backoff/jitter bounds;
- result queue/cancellation releases outstanding state;
- HTTP diagnostics preserve native error/status/byte counts;
- landscape chart coordinates stay inside 320×170;
- chart range includes previous-close and opening-price references;
- invalid open suppresses open line;
- lunch discontinuity remains intact.

Full regression:

```bash
pio test -e native
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
pio run -e lilygo-t-display-s3
```

## 10. Hardware acceptance

After automated tests pass, physical validation must use the exact Git commit SHA.

Minimum stability acceptance:

- 5 configured stocks;
- at least 100 button switches without visible UI lockup;
- no watchdog reset or unexpected reboot;
- last valid quote/chart remain visible through request failures;
- one intraday failure does not stop quote refresh;
- current quote P95 refresh interval <= 7 s with 5 s configuration under healthy quote-provider conditions;
- single intraday failure should not create a quote gap > 10 s;
- collect at least 500 quote attempts, target quote success >= 99% under the test network;
- collect at least 30 intraday refresh cycles, target cycle success >= 80% after bounded retries;
- intraday P95 age <= 180 s during trading;
- if intraday cycle success remains below 80%, open a separate design review for an intraday fallback provider instead of weakening parsing or retry limits;
- final endurance target: one complete 09:25–15:10 trading-day run with zero crash/reset/cache-loss events.

## 11. Expected code areas

Likely modifications:

- `src/network/MarketDataWorker.h`
- `src/network/MarketDataWorker.cpp`
- `src/network/HttpTransport.h`
- `src/network/HttpTransport.cpp`
- `lib/providers/IQuoteProvider.h` only if diagnostics need to cross the Provider boundary
- `src/network/EastMoneyProvider.cpp`
- `src/network/TencentProvider.cpp` only for compatible diagnostics plumbing; Tencent remains quote-only fallback
- `src/app/StockController.h`
- `src/app/StockController.cpp`
- `src/device/DeviceLayer.cpp`
- `src/ui/StockScreen.h`
- `src/ui/StockScreen.cpp`
- `include/build_config.h`
- related native tests and acceptance documentation.

## 12. Explicit non-goals

- No new intraday provider in this change.
- No concurrent quote/intraday TLS workers unless post-change measurements prove single-worker QoS cannot meet quote-latency targets.
- No parser relaxation.
- No certificate-verification downgrade.
- No broker/trading functionality.
