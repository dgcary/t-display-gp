# Data Provider / HTTP API Contract

Status date: 2026-08-20

T-Display GP keeps remote payload formats behind Provider abstractions. UI/Controller code consumes structured data and must not parse raw provider bodies directly. Local-only apps such as NixieClock must remain outside the remote Provider/HTTP path.

## Provider matrix

| Capability | Primary | Fallback |
|---|---|---|
| A-share quote | Tencent | EastMoney |
| A-share intraday | Tencent | EastMoney |
| Weather current + 3-day forecast | Open-Meteo | none |
| Nixie local clock | Device local time / common NTP state | none; no HTTP provider |

Public provider contracts may change. Strict parsing and cache preservation are preferred over guessing new payload semantics.

# Market data

## Tencent quote primary

```text
GET https://qt.gtimg.cn/q=<market-prefix><code>
```

Tencent is the normal quote source. Quote provider health is independent from intraday health.

Primary quote failover policy:

- startup/default provider: Tencent,
- 3 Tencent quote failures inside the existing 60-second failure window switch quote traffic to EastMoney,
- while EastMoney is active, Tencent is probed at the existing 120-second probe interval,
- 2 successful Tencent recovery probes restore Tencent as primary,
- a failed Tencent recovery probe resets the consecutive recovery-success count,
- EastMoney failure does not create an unbounded provider chain; the last valid quote cache is preserved.

Tencent quote parsing remains strict: mismatched symbol or malformed fields are rejected and never replace the last valid quote cache.

## Tencent intraday primary

```text
GET https://web.ifzq.gtimg.cn/appstock/app/minute/query?code=<market-prefix><code>
```

Every new intraday cycle starts with Tencent, independent from the current quote provider.

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

Tencent transient intraday errors use the existing bounded/deferred retry policy before EastMoney fallback is considered.

## EastMoney quote fallback

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

EastMoney is a quote fallback only in the current provider policy; it is not the normal startup source.

## EastMoney intraday fallback

```text
GET https://push2his.eastmoney.com/api/qt/stock/trends2/get?secid=<secid>&fields1=f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11&fields2=f51,f52,f53,f54,f55,f56,f57,f58&ndays=1&iscr=0&iscca=0
Referer: https://quote.eastmoney.com/
```

Each row uses the existing time/price/volume/average-price parser contract. Only 09:30–11:30 and 13:00–15:00 are retained; the series remains capped at 242 points.

EastMoney intraday is installed only after the Tencent intraday cycle is exhausted or Tencent returns a non-retryable intraday Provider error. EastMoney failure is terminal for that logical cycle and never recursively creates another fallback.

## Market Worker contract

Request priority remains:

1. latest current-page quote
2. background quote
3. Tencent primary-recovery probe
4. intraday
5. intraday retry

Waiting intraday remains latest-wins.

Approved TTLs remain:

- current quote: 8 s
- background quote: 12 s
- primary probe: 30 s
- normal intraday: 75 s
- Tencent intraday retry cycle: 15 s from first attempt

Tencent intraday transient retry remains max 3 total attempts with approximately 1.5 s then 4 s deferred delays (with existing jitter) and must yield to quote work.

After the Tencent intraday cycle is exhausted, or Tencent returns a non-retryable intraday Provider error, `MarketDataWorker` may install an EastMoney intraday fallback using the same logical request id. EastMoney failure is terminal for that cycle.

A complete intraday cycle failure does not immediately reopen a new empty-cache cycle. The controller records explicit request history and waits the normal intraday refresh interval before trying Tencent again. This avoids retry storms and does not use `millis()==0` as an uninitialized sentinel.

Intraday provider selection is independent from quote provider selection. A quote may temporarily be on EastMoney while a new intraday cycle still begins on Tencent; conversely Tencent/EastMoney intraday success or failure must not change quote failover health.

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

# Nixie Clock local-time contract

NixieClockApp has **no remote data provider**. It must not call `HttpTransport`, `HTTPClient`, `WiFiClientSecure`, `AppDataWorker`, or `NetworkArbiter`.

Its time source is the same device-local time path already maintained by the firmware:

```text
common boot NTP configuration
        -> ESP32 system clock
        -> DeviceLayer::localDateTime()
        -> NixieClockModel
        -> NixieClockScreen
```

Contract:

- no new NTP task/client is created for NixieClock,
- `LocalDateTime` is sampled at approximately 1-second cadence while NixieClock is active,
- year/month/day/hour/minute/second/day-of-week are range-validated before display,
- unsynchronized/invalid time fails closed and displays a waiting state,
- the colon may use a local `millis()`-based 500 ms visual phase; this does not alter the authoritative wall-clock source,
- NixieClock adds no config schema/API fields,
- entering or leaving NixieClock must not create `[md]` or `[appdata]` traffic,
- `tools/validate_nixie_clock_contract.py` guards the local-only/menu integration boundary.

# Shared HTTP/TLS transport

Market and app-data Providers use the same `HttpTransport` implementation. NixieClock does not use this implementation.

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

When the Tencent intraday cycle hands off to EastMoney, a concise marker is emitted:

```text
[md] id=... type=INTRADAY symbol=... fallback=TX->EM
```

The stock footer exposes actual data sources independently. Common examples:

```text
Q:TX
Q:TX I:TX
Q:TX I:EM
Q:EM I:TX
```

`I:` appears once a valid intraday cache exists.

## App data / Weather

```text
[appdata] id=... type=WEATHER location=... queue=...ms dur=...ms http=... native=... tls=... bytes=.../... result=...
```

## Nixie Clock

No Nixie-specific network log is expected. While active, system resource logging identifies it through:

```text
[sys] app=NIXIE_CLOCK ...
```

Unexpected `[md]`/`[appdata]` work that is directly caused by NixieClock entry is a contract failure.

## Runtime memory

```text
[sys] app=STOCK|MENU|WEATHER|NIXIE_CLOCK|DEVICE_INFO heap_free=... heap_min=... psram_free=... psram_total=... main_stack_hwm=...
```

Do not log full provider response bodies by default.

# Cache / error isolation

Stock quote, stock intraday, and weather each maintain independent cache/health semantics. NixieClock only consumes local clock state and has no provider health/cache coupling.

Examples:

- weather failure cannot change stock quote provider state
- intraday failure cannot clear quote cache
- intraday fallback cannot change quote failover health
- stock failure cannot clear weather snapshot
- entering NixieClock cannot change provider health or enqueue external network work
- inactive app network completion cannot redraw the TFT

# Validation policy

When a provider changes/fails:

1. capture real device diagnostics,
2. reproduce with a regression fixture/behavior test,
3. change Provider/transport boundary rather than UI where possible,
4. never weaken parser simply to accept unknown malformed data,
5. do not add infinite retry or unbounded timeout as a workaround.

For NixieClock changes, validate the local-time/model/render contract instead of inventing a remote Provider.
