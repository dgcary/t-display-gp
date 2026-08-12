# Market Data API Contract

Status date: 2026-08-11

This document records the V1 provider contract implemented by T-Display GP. These are public, unauthenticated endpoints and are not vendor-supported APIs. The provider layer is intentionally replaceable because response formats or access policy may change without notice.

## Provider matrix

| Capability | EastMoney | Tencent |
|---|---|---|
| Quote snapshot | Primary | Fallback |
| Intraday trend | Primary | Not used in V1 |
| SSE symbol mapping | `1.<code>` | `sh<code>` |
| SZSE symbol mapping | `0.<code>` | `sz<code>` |
| BSE symbol mapping | `0.<code>` | `bj<code>` candidate |
| Parser fixture coverage | Yes | Yes |
| Live raw endpoint validation in this environment | Pending | SSE quote checked; SZ/BSE pending |

The BSE stock `920047` is recognized by EastMoney's public quote site. The raw EastMoney `push2` endpoint and Tencent BSE `bj` endpoint still require validation on an unrestricted network/device before being marked accepted.

## EastMoney quote

Request:

```text
GET https://push2.eastmoney.com/api/qt/stock/get?secid=<secid>&fields=f57,f58,f43,f44,f45,f46,f47,f48,f60,f86,f169,f170
Referer: https://quote.eastmoney.com/
```

Fields consumed by V1:

| Field | Meaning in V1 | Decode |
|---|---|---|
| `f57` | stock code | string; must match requested symbol |
| `f58` | stock name | UTF-8 string |
| `f43` | current price | integer-like values ÷ 100; decimal values used directly |
| `f44` | high | same price scaling |
| `f45` | low | same price scaling |
| `f46` | open | same price scaling |
| `f47` | volume | unsigned integer |
| `f48` | amount | numeric, used directly |
| `f60` | previous close | same price scaling |
| `f86` | quote time | Unix epoch seconds |
| `f169` | change | price scaling |
| `f170` | change percent | price scaling |

A missing, malformed, stale-symbol or structurally invalid response is rejected; the caller keeps the previously cached value.

## EastMoney intraday trend

Request:

```text
GET https://push2his.eastmoney.com/api/qt/stock/trends2/get?secid=<secid>&fields1=f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11&fields2=f51,f52,f53,f54,f55,f56,f57,f58&ndays=1&iscr=0&iscca=0
Referer: https://quote.eastmoney.com/
```

V1 consumes each `trends` CSV row as:

```text
<datetime>,<price>,...,<volume>,...,<average-price>
```

Specifically: column 0 time, column 1 price, column 5 volume, column 7 average price. Only 09:30–11:30 and 13:00–15:00 points are retained; duplicate/out-of-order minutes are skipped and the series is capped at 242 points.

## Tencent quote fallback

Request:

```text
GET https://qt.gtimg.cn/q=<market-prefix><code>
```

Expected body form:

```text
v_sh600519="...~...";
```

Fields consumed by zero-based tilde index:

| Index | Meaning in V1 |
|---:|---|
| 1 | stock name |
| 2 | stock code |
| 3 | current price |
| 4 | previous close |
| 5 | open |
| 6 | volume |
| 30 | `YYYYMMDDhhmmss` China-local quote timestamp |
| 31 | change |
| 32 | change percent |
| 33 | high |
| 34 | low |
| 37 | amount in ten-thousand currency units; V1 multiplies by 10,000 |

The timestamp is converted from UTC+8 local civil time to Unix UTC epoch seconds. The timestamp is required because the controller uses provider dates to distinguish a stale prior-session quote from current-session data.

## HTTP transport contract

- HTTP status must be exactly 200.
- Connect timeout: 1500 ms.
- Read timeout: 2500 ms.
- Maximum body retained in RAM: 32 KiB.
- A declared or streamed body larger than the limit is rejected.
- Chunked responses are consumed through `HTTPClient::writeToStream` into a bounded sink.
- V1 uses `WiFiClientSecure::setInsecure()` for these public quote endpoints. TLS encryption remains present but endpoint certificate identity is not verified.
- Market HTTP runs only in the FreeRTOS market worker; the UI loop never performs HTTP.

## Failover contract

1. EastMoney is the normal quote provider.
2. Three EastMoney quote failures within 60 seconds switch active quote traffic to Tencent.
3. While Tencent is active, EastMoney is probed at most once every 120 seconds.
4. Two successful EastMoney probes restore EastMoney as active provider.
5. A failed recovery probe resets the recovery-success count.
6. Intraday remains EastMoney-only in V1; a quote fallback does not erase the cached EastMoney intraday chart.
7. Network/provider failure never clears a previously valid quote or intraday cache.

## Validation status

Automated parser/provider/controller fixtures cover SSE, SZSE and BSE symbol mapping, malformed payload rejection, quote-date handling, provider fallback/recovery and retained-cache behavior.

During the 2026-08-11 development session, a current Tencent SSE response for `sh600519` matched the implemented tilde-field contract and carried a `20260811161401` timestamp. Full live raw validation for EastMoney `push2`, Tencent SZSE, and Tencent BSE is still a hardware/unrestricted-network acceptance item, not an automated-pass claim.
