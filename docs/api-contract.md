# Market Data API Contract

Status date: 2026-08-18

T-Display GP uses public, unauthenticated, unofficial market endpoints. Provider and parser boundaries remain replaceable because formats/access policy may change.

## Provider matrix

| Capability | EastMoney | Tencent |
|---|---|---|
| Quote | Primary | Fallback |
| Intraday trend | Primary | Not used in V1 |
| SSE | `1.<code>` | `sh<code>` |
| SZSE | `0.<code>` | `sz<code>` |
| BSE | `0.<code>` | `bj<code>` candidate; physical validation required |

## EastMoney quote

```text
GET https://push2.eastmoney.com/api/qt/stock/get?secid=<secid>&fields=f57,f58,f43,f44,f45,f46,f47,f48,f60,f86,f169,f170
Referer: https://quote.eastmoney.com/
```

Consumed fields:

| Field | Meaning |
|---|---|
| `f57` | code; must match requested symbol |
| `f58` | UTF-8 name |
| `f43` | last |
| `f44` | high |
| `f45` | low |
| `f46` | open |
| `f47` | volume |
| `f48` | amount |
| `f60` | previous close |
| `f86` | Unix quote timestamp |
| `f169` | change |
| `f170` | change percent |

Integer-like price/change values use the existing ÷100 scaling rule; decimal values are used directly. Missing, malformed, mismatched-symbol or structurally invalid payloads are rejected and do not replace cache.

## EastMoney intraday

```text
GET https://push2his.eastmoney.com/api/qt/stock/trends2/get?secid=<secid>&fields1=f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11&fields2=f51,f52,f53,f54,f55,f56,f57,f58&ndays=1&iscr=0&iscca=0
Referer: https://quote.eastmoney.com/
```

Each trend row uses column 0 time, 1 price, 5 volume and 7 average price. Only 09:30–11:30 and 13:00–15:00 are retained. Duplicate/out-of-order minutes are skipped; series is capped at 242 points.

Parser strictness is unchanged by the stability work. Unknown malformed payloads are not accepted merely to improve apparent success rate.

## Tencent quote fallback

```text
GET https://qt.gtimg.cn/q=<market-prefix><code>
```

Important zero-based fields:

| Index | Meaning |
|---:|---|
| 1 | name |
| 2 | code |
| 3 | last |
| 4 | previous close |
| 5 | open |
| 6 | volume |
| 30 | China-local `YYYYMMDDhhmmss` timestamp |
| 31 | change |
| 32 | change percent |
| 33 | high |
| 34 | low |
| 37 | amount (V1 converts ten-thousand units to base units) |

Tencent remains quote-only fallback. No intraday fallback is added in this change.

## HTTP transport contract

- HTTP 200 is required for successful Provider parsing.
- Connect timeout: 1500 ms.
- Read timeout: 2500 ms.
- Maximum retained response body: 32 KiB.
- Declared/streamed oversize responses are rejected.
- Content-Length mismatch after an otherwise successful read is classified as truncated transport failure.
- `HTTPClient::setReuse(false)` remains unchanged for this stability release.
- Current V1 keeps its existing `WiFiClientSecure::setInsecure()` behavior; certificate-verification hardening is a separate security task.
- All market HTTP executes in `MarketDataWorker`; UI/main loop does not perform blocking market HTTP.

Transport diagnostics preserve:

- HTTP status
- native HTTPClient error code
- TLS last error when available
- expected Content-Length
- received byte count
- elapsed time

## Worker scheduling contract

Request priority:

1. latest current-page quote
2. background quote
3. EastMoney recovery probe
4. intraday
5. intraday retry

Waiting intraday uses latest-wins semantics: only one not-yet-started intraday item is retained. A newer current-stock trend request replaces an older pending trend request; the replaced request produces an explicit cancellation result so Controller outstanding state is released.

TTL:

- quote / primary probe: 15 s
- intraday / intraday retry: 10 s

Expired accepted requests produce an explicit expiry result rather than silently disappearing.

## Intraday retry contract

Only transient failures are eligible:

- transport/network failure, including connection loss/read failure/truncated body
- HTTP 408
- HTTP 5xx

No immediate retry for:

- parser/schema errors
- missing fields
- body too large
- unsupported data
- ordinary HTTP 4xx other than 408

Maximum: **3 total attempts per intraday refresh cycle**.

Deferred delays:

```text
attempt 2: ~1500 ms ±20%
attempt 3: ~4000 ms ±20%
```

Retry never loops inside the Provider; quote work can run between attempts.

## Quote failover contract

1. EastMoney is normal quote Provider.
2. Three EastMoney quote failures within 60 s switch quote traffic to Tencent.
3. While Tencent is active, EastMoney recovery probe is no faster than once per 120 s.
4. Two successful probes restore EastMoney.
5. Failed probe resets recovery-success count.
6. Intraday failure does not trigger Tencent intraday use.

## Cache and health contract

Quote and intraday have independent health state:

- last error
- last attempt
- last success
- consecutive failures

A quote success clears only quote health. An intraday success clears only intraday health. Failure never erases the corresponding last valid payload.

UI thresholds:

- quote age >=15 s: `报价延迟`
- intraday age >=180 s: `分时延迟`
- one isolated intraday failure with a fresh cached chart: no generic page-wide error

## Request log contract

Each completed/expired/cancelled request emits a concise `[md]` line. Example:

```text
[md] id=182 type=INTRADAY symbol=000831 provider=EM attempt=2/3 queue=8ms dur=2680ms http=200 native=-5 tls=-29184 bytes=8192/13824 result=NETWORK
```

Do not log full response bodies by default.

## Validation policy

PC `curl`/Windows Schannel behavior is useful external evidence, but is not treated as proof of the ESP32 failure type. Device-side `[md]` diagnostics are the source used to classify actual ESP32 transport failures.

If at least 30 physical intraday refresh cycles still have <80% final-cycle success after bounded retries, open a separate design review for an intraday fallback Provider rather than weakening parser or retry limits.
