# Data Provider / HTTP API Contract

Status date: 2026-08-19

T-Display GP keeps remote payload formats behind Provider abstractions. UI/Controller code must consume structured data and must not parse raw provider bodies directly.

## Provider matrix

| Capability | Primary | Fallback |
|---|---|---|
| A-share quote | EastMoney | Tencent |
| A-share intraday | EastMoney | Tencent |
| Weather current + 3-day forecast | Open-Meteo | none |

Public provider contracts may change. Strict parsing and cache preservation are preferred over guessing new payload semantics.

# Market data

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

Missing/malformed/mismatched-symbol payloads are rejected and never replace the last valid quote cache.

## EastMoney intraday primary

```text
GET https://push2his.eastmoney.com/api/qt/stock/trends2/get?secid=<secid>&fields1=f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11&fields2=f51,f52,f53,f54,f55,f56,f57,f58&ndays=1&iscr=0&iscca=0
Referer: https://quote.eastmoney.com/
```

Each row uses time, price, volume and average price fields already covered by parser regression tests. Only 09:30–11:30 and 13:00–15:00 are retained; series remains capped at 242 points.

## Tencent quote fallback

```text
GET https://qt.gtimg.cn/q=<market-prefix><code>
```

Quote fallback is independent from intraday fallback. Quote health continues to use the existing EastMoney failure window/recovery-probe policy.

## Tencent intraday fallback

```text
GET https://web.ifzq.gtimg.cn/appstock/app/minute/query?code=<market-prefix><code>
```

Tencent minute rows are parsed strictly into the existing `IntradaySeries` abstraction. The parser expects provider rows containing:

```text
HHMM price cumulative_lots cumulative_amount
```

Rules:

- only 09:30–11:30 and 13:00–15:00 are retained,
- rows must be strictly increasing by minute,
- price must be finite and positive,
- cumulative lots and amount must be valid non-negative numeric fields,
- average price is derived from cumulative amount / (`lots * 100`) when cumulative volume is non-zero,
- the retained series remains capped at 242 points,
- malformed/missing/empty-session payloads are rejected without replacing the previous chart cache.

Tencent intraday is a fallback only. Every new intraday cycle still begins with EastMoney.

## Market Worker contract

Request priority remains:

1. latest current-page quote
2. background quote
3. EastMoney recovery probe
4. intraday
5. intraday retry

Waiting intraday remains latest-wins.

Approved TTLs remain:

- current quote: 8 s
- background quote: 12 s
- primary probe: 30 s
- normal intraday: 75 s
- EastMoney intraday retry cycle: 15 s from first attempt

EastMoney intraday transient retry remains max 3 total attempts with approximately 1.5 s then 4 s deferred delays (with existing jitter) and must yield to quote work.

After the EastMoney intraday cycle is exhausted, or EastMoney returns a non-retryable intraday Provider error, MarketDataWorker may install a Tencent intraday fallback using the same logical request id. Tencent failure is terminal for that cycle and never recursively creates another fallback.

A complete intraday cycle failure does not immediately reopen a new empty-cache cycle. The controller records explicit request history and waits the normal intraday refresh interval before trying EastMoney again. This avoids retry storms and does not use `millis()==0` as an uninitialized sentinel.

Intraday provider selection is independent from quote provider selection. A quote may be on Tencent while a new intraday cycle still begins on EastMoney; conversely a Tencent intraday success must not change quote failover health.

When StockApp is suspended, MarketDataWorker is paused for new/pending execution. A request that had already begun external HTTP is allowed to finish naturally; suspension does not force-delete a TLS task. Intraday retry/fallback installation is suppressed while paused. On StockApp re-entry, worker execution resumes and existing TTL/expiry semantics handle retained pending work.

# Weather data

## Open-Meteo request

V1 WeatherApp uses the forecast endpoint through `IWeatherProvider` / `OpenMeteoProvider`.

Conceptual request:

```text
GET https://api.open-meteo.com/v1/forecast
    ?latitude=<6 decimal degrees>
    &longitude=<6 decimal degrees>
    &current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m
    &daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max
    &timezone=Asia%2FShanghai
    &forecast_days=3
    &timeformat=unixtime
```

Configuration stores latitude/longitude as integer microdegrees and formats them as six-decimal coordinates only at the Provider boundary.

## Weather fields retained

The parser converts the remote payload into bounded `WeatherSnapshot` state:

```text
currentTemp
apparentTemp
humidityPercent
windSpeed
precipitationProbabilityPercent
weatherCode
today high/low/code
tomorrow high/low/code
dayAfter high/low/code
updatedEpochSeconds
```

Raw JSON is not retained after successful parse.

Parser requirements:

- `current` object must exist
- required current fields must exist and be numeric
- daily code/high/low/precipitation arrays must contain at least three entries
- humidity and precipitation remain 0..100
- weather code remains in expected numeric range
- temperature/wind values must be finite and within broad sanity ranges
- daily low may not exceed daily high

Provider parses into a temporary object and assigns output only after full validation. Parse/missing-field failure does not mutate the caller's existing valid snapshot.

## Weather refresh contract

- default: 15 minutes
- configurable: 5..60 minutes
- first active+online WeatherApp tick with no request history may enqueue immediately
- subsequent scheduling is paced from **last request attempt time**, not last success time
- therefore a failed refresh cannot cause an immediate tight retry loop
- last success timestamp is used for cache age/stale indication, not scheduling
- no active scheduling while WeatherApp is suspended
- provider/network/parser failure preserves the last valid cache

Weather has no V1 provider fallback and no rapid retry loop.

# Shared HTTP/TLS transport

Market and app-data Providers use the same `HttpTransport` implementation.

## NetworkArbiter

All external HTTP/TLS operations acquire a shared firmware mutex before creating/using the secure client:

> **At most one external HTTP/TLS request executes at a time.**

This is an execution/memory boundary, not a Provider priority scheduler. MarketDataWorker and AppDataWorker keep their own queue policies, then serialize at the actual external TLS operation.

The lock is RAII-managed so every normal/error return releases it.

## Transport limits

- HTTP 200 required before Provider parsing
- TCP/connect timeout: 1500 ms
- TLS handshake timeout: 5 s
- HTTP/read timeout setting: 2500 ms
- retained response body maximum: 32 KiB
- declared/streamed oversize responses rejected
- content-length mismatch classified as truncated transport failure
- `HTTPClient::setReuse(false)` remains enabled
- do not pass the 2500-ms read constant directly into the seconds-based Arduino-ESP32 2.0.14 `WiFiClientSecure::setTimeout()`

Current public-data clients retain the project's pre-existing `WiFiClientSecure::setInsecure()` behavior. The multi-app/weather work does not treat that as an acceptable security boundary for future sensitive Home Assistant credentials; token storage and strict TLS must receive a separate security design before HA write/control features are added.

`tools/validate_http_transport_contract.py` guards the timeout/reuse/arbiter source contract in CI.

# Diagnostics

## Market

```text
[md] id=... type=QUOTE|INTRADAY|PROBE symbol=... provider=EM|TX attempt=... queue=...ms dur=...ms http=... native=... tls=... bytes=.../... result=...
```

When an EastMoney intraday cycle hands off to Tencent, a concise marker is emitted:

```text
[md] id=... type=INTRADAY symbol=... fallback=EM->TX
```

The stock footer exposes the actual data sources independently:

```text
Q:EM
Q:TX I:EM
Q:TX I:TX
```

`I:` appears once a valid intraday cache exists.

## App data / Weather

```text
[appdata] id=... type=WEATHER location=... queue=...ms dur=...ms http=... native=... tls=... bytes=.../... result=...
```

## Runtime memory

```text
[sys] app=STOCK|MENU|WEATHER|DEVICE_INFO heap_free=... heap_min=... psram_free=... psram_total=... main_stack_hwm=...
```

Do not log full provider response bodies by default.

# Cache / error isolation

Stock quote, stock intraday, and weather each maintain independent cache/health semantics.

Examples:

- weather failure cannot change stock quote provider state
- intraday failure cannot clear quote cache
- Tencent intraday fallback cannot change quote failover health
- stock failure cannot clear weather snapshot
- inactive app network completion cannot redraw the TFT

# Validation policy

When a provider changes/fails:

1. capture real device diagnostics,
2. reproduce with a regression fixture/behavior test,
3. change Provider/transport boundary rather than UI where possible,
4. never weaken parser simply to accept unknown malformed data,
5. do not add infinite retry or unbounded timeout as a workaround.
