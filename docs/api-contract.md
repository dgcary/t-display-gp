# Data Provider / HTTP API Contract

Status date: 2026-08-20

Remote payloads stay behind Provider abstractions. UI/controllers consume structured state and must not parse raw provider bodies directly. Local-only apps remain outside the remote Provider path.

## Provider matrix

| Capability | Primary | Fallback / notes |
|---|---|---|
| A-share quote | Tencent | EastMoney |
| A-share intraday | Tencent | EastMoney |
| Weather | Open-Meteo | none |
| Nixie time | Device local system clock | no HTTP |
| Home Assistant | user's existing HA server | read-only REST client |
| Crypto | Binance market-data-only host | none |

# A-share market data

## Tencent quote

```text
GET https://qt.gtimg.cn/q=<market-prefix><code>
```

Tencent is normal quote primary. Parser rejects mismatched symbol/malformed payloads and never replaces valid cache on failure.

Quote failover:

- 3 Tencent quote failures in the existing failure window -> EastMoney.
- while EastMoney active, probe Tencent at existing 120-second interval.
- 2 consecutive successful Tencent probes -> Tencent primary restored.
- failed probe resets recovery-success count.

## Tencent intraday

```text
GET https://web.ifzq.gtimg.cn/appstock/app/minute/query?code=<market-prefix><code>
```

Every new intraday cycle begins Tencent, independent of quote provider. Only trading-session rows are retained; rows must be ordered and numeric. Series remains bounded.

Transient Tencent failures use existing bounded/deferred retries, max 3 total attempts, and yield to quote traffic. Final/non-retryable failure may hand the same logical cycle to EastMoney.

## EastMoney quote fallback

```text
GET https://push2.eastmoney.com/api/qt/stock/get?secid=<secid>&fields=f57,f58,f43,f44,f45,f46,f47,f48,f60,f86,f169,f170
Referer: https://quote.eastmoney.com/
```

Required symbol/price/volume/time fields remain strictly validated.

## EastMoney intraday fallback

```text
GET https://push2his.eastmoney.com/api/qt/stock/trends2/get?secid=<secid>&fields1=...&fields2=...&ndays=1&iscr=0&iscca=0
Referer: https://quote.eastmoney.com/
```

EastMoney is terminal secondary for a logical intraday cycle; its failure does not recursively create another fallback. A full cycle failure waits the normal refresh interval.

## MarketDataWorker QoS

Priority remains:

1. current-page quote
2. background quote
3. Tencent recovery probe
4. intraday
5. intraday retry

Waiting intraday is latest-wins. Quote/intraday health is independent.

Approved TTL behavior and existing request-expiry semantics remain unchanged.

# Weather

V1 uses Open-Meteo forecast API with configured latitude/longitude, current weather and 3-day daily fields.

Structured `WeatherSnapshot` includes:

```text
currentTemp
apparentTemp
humidityPercent
windSpeed
precipitationProbabilityPercent
weatherCode
today / tomorrow / dayAfter high-low-code
updatedEpochSeconds
```

Rules:

- required objects/fields must exist and be numeric.
- humidity/precipitation 0..100.
- finite, broadly sane temperature/wind values.
- daily low <= high.
- parse into temporary snapshot, assign output only after complete validation.
- default 15-minute refresh; config 5..60 min.
- pace from last request attempt, not last success; no tight retry.
- active-only scheduling; failure preserves cache.

# Nixie Clock

No remote provider.

```text
common NTP configuration
 -> ESP32 system clock
 -> DeviceLayer::localDateTime()
 -> NixieClockModel
 -> NixieClockScreen
```

Nixie must not call `HttpTransport`, `HTTPClient`, `WiFiClientSecure`, `AppDataWorker` or `NetworkArbiter`. Invalid time fails closed. It is the default startup app and system idle destination.

# Home Assistant

## Role

**T-Display-S3 is only a REST API client. The user's existing Home Assistant installation is the server.**

The device does not expose or emulate HA APIs. Its own `:8081` page is only a local configuration UI.

## Entity read

For each configured entity:

```text
GET <base_url>/api/states/<entity_id>
Authorization: Bearer <long-lived-access-token>
Accept: application/json
```

V1 is read-only. No `/api/services` write/control calls are allowed.

Retained entity fields:

```text
entity_id
state
attributes.friendly_name (optional)
attributes.unit_of_measurement (optional)
```

Rules:

- configured entity ID must match response `entity_id`.
- `state` required and bounded.
- optional friendly name/unit bounded.
- parse failures do not mutate the previous entity cache.
- 1..4 configured entities.
- sequential request cycle.
- refresh 30..300 sec, default 30 sec.
- active-only scheduling.

## Home Assistant HTTP mode

A base URL beginning `http://` uses ordinary `WiFiClient`.

This intentionally supports common existing LAN HA installations such as:

```text
http://homeassistant.local:8123
http://192.168.x.x:8123
```

No CA is required. The Bearer token is cleartext to LAN observers, so this mode is only for a trusted LAN.

## Home Assistant HTTPS mode

A base URL beginning `https://` requires a configured PEM CA. Transport uses:

```text
WiFiClientSecure
setCACert(...)
```

`setInsecure()` is forbidden for the Home Assistant credentialed HTTPS path.

Both HTTP and HTTPS HA requests:

- acquire `NetworkArbiter`.
- retain at most 4 KiB response body.
- use existing 1500 ms connect / 2500 ms read settings.
- disable HTTP reuse.
- never log Token/Authorization contents.
- log transport mode as `HA_HTTP` or `HA_CA`.

Unknown URL schemes are invalid. HTTPS without valid CA is invalid.

# Crypto

V1 uses Binance's dedicated market-data-only host and requires no API credential.

Conceptual request:

```text
GET https://data-api.binance.vision/api/v3/ticker/24hr
    ?symbols=["BTCUSDT","ETHUSDT","SOLUSDT"]
```

Firmware URL-encodes the JSON symbol list.

Retained snapshot order is fixed logically as BTC / ETH / SOL, but provider response order is **not** trusted. Parser maps rows by `symbol`.

Required per ticker:

```text
symbol
lastPrice
priceChangePercent
closeTime
```

Rules:

- exactly one valid row for BTCUSDT, ETHUSDT, SOLUSDT.
- duplicate/unknown/missing symbols rejected.
- price/change strings must parse completely to finite numbers.
- price > 0; timestamp > 0.
- parse entire response into temporary `CryptoSnapshot`; assign only after all 3 succeed.
- fixed V1 refresh 60 sec.
- active-only scheduling.
- failure preserves last complete snapshot.

# Shared AppDataWorker contract

Weather / Home Assistant / Crypto use exactly one shared worker task.

```text
request queue
    -> AppDataWorker
       -> WEATHER provider
       -> HOME_ASSISTANT provider
       -> CRYPTO provider

results
    -> per-AppDataRequestType result queues
```

Typed result queues prevent one App consuming/dropping a delayed result belonging to another App. FreeRTOS queues store pointers to allocated C++ request/result objects rather than byte-copying objects containing `std::string`.

An already-running request may complete after an app exits, but inactive apps do not schedule follow-up cycles and do not redraw TFT.

# NetworkArbiter / public HttpTransport

All actual external HTTP/TLS operations serialize through `NetworkArbiter`: at most one external request executes at once.

Public market/weather/crypto `HttpTransport` keeps:

```text
connect timeout 1500 ms
HTTP/read setting 2500 ms
TLS handshake cap 5 s
retained body max 32 KiB
HTTPClient::setReuse(false)
```

Public-data clients retain the project's existing `setInsecure()` behavior. This trust model **does not extend to HA HTTPS credentials**.

# Diagnostics

Market:

```text
[md] id=... type=QUOTE|INTRADAY|PROBE ... provider=TX|EM ...
```

Intraday handoff:

```text
[md] id=... type=INTRADAY symbol=... fallback=TX->EM
```

Low-frequency app data:

```text
[appdata] ... type=WEATHER ...
[appdata] ... type=HOME_ASSISTANT entity=... ...
[appdata] ... type=CRYPTO ...
```

Network timing:

```text
[net] host=... mode=HA_HTTP|HA_CA arb=... io=... total=... http=... tls=...
```

Runtime resources:

```text
[sys] app=MENU|STOCK|WEATHER|NIXIE_CLOCK|HOME_ASSISTANT|CRYPTO|DEVICE_INFO ...
```

Do not log full provider bodies or credentials by default.

# Cache/error isolation

- weather failure cannot alter Stock health.
- intraday failure cannot clear quote cache or alter quote provider health.
- Stock failure cannot clear Weather/HA/Crypto caches.
- HA entity failure preserves that entity's previous state.
- Crypto failure preserves last complete 3-symbol snapshot.
- Nixie entry cannot create network work.
- inactive late completion cannot redraw the TFT.

When providers fail, capture real diagnostics first, add/update regression tests, fix at the provider/transport boundary, and never weaken strict parsing merely to increase apparent success rate.
