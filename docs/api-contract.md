# Data Provider / HTTP API Contract

Status date: 2026-08-25

Remote payloads stay behind Provider abstractions. UI/controllers consume structured state and do not parse raw provider bodies directly. Local-only apps/media playback remain outside remote Provider paths.

## Provider matrix

| Capability | Primary | Fallback / notes |
|---|---|---|
| A-share quote | Tencent | EastMoney |
| A-share intraday | Tencent | EastMoney |
| Weather | Open-Meteo | none |
| Nixie time | Device local system clock | no HTTP |
| Home Assistant | **user's existing HA server** | T-Display read-only REST client |
| Crypto | Binance market-data-only | none |
| Weather Bad Apple playback | local compiled flash asset | no runtime HTTP/task |

# A-share market data

Tencent quote: `https://qt.gtimg.cn/q=<market-prefix><code>`.

Tencent intraday: `https://web.ifzq.gtimg.cn/appstock/app/minute/query?code=<market-prefix><code>`.

EastMoney quote fallback uses `push2.eastmoney.com`; intraday fallback uses `push2his.eastmoney.com` with existing referer/field contracts.

Rules remain:

- Tencent quote + intraday primary.
- EastMoney secondary/fallback.
- quote/intraday provider health independent.
- every new intraday cycle starts Tencent.
- transient Tencent intraday retry bounded/deferred, max 3 attempts, yields to quotes.
- EastMoney intraday failure terminal for that logical cycle.
- full intraday failure waits normal refresh; no empty-cache retry storm.
- quote priority > intraday; waiting intraday latest-wins.
- 3 Tencent quote failures within existing window switch quote traffic to EastMoney; while secondary, probe Tencent at existing interval; 2 consecutive successful probes recover.
- parsers fail-closed and preserve last valid caches.

# Weather

V1 uses Open-Meteo current weather + three-day forecast with configured location.

Structured state still includes current temperature/apparent/humidity/wind/precipitation/weather code, 3 daily high-low-code sets and updated time. Required fields/ranges are validated; parse occurs into a temporary snapshot and assigns only after complete success. Default refresh 15 min, configurable 5–60 min, paced from last attempt, active-only, failure preserves cache.

The **network/provider contract is unchanged** by the Bad Apple UI. Presentation intentionally consumes only:

```text
current weather
weather.today
weather.tomorrow
```

`weather.dayAfter` remains valid structured provider/cache data but is not rendered on the 320×170 Weather screen so the right side can host the larger video viewport.

## Weather Bad Apple media path

Bad Apple is a build-time media transformation, not a runtime remote Provider:

```text
pinned source video + verified Git blob SHA1
  -> ffmpeg: 168x126, 10 FPS, 2190 frames
  -> threshold to 1-bit
  -> first frame + XOR sparse-delta encoding
  -> full generator round-trip verification
  -> generated BadAppleAsset.*
  -> firmware.bin
  -> WeatherScreen local playback
```

Runtime contract:

- viewport x=152, y=27, width=168, height=126.
- 2190 frames @ 10 FPS, ~219 s, silent, looping.
- Weather entry starts frame 0; Weather exit stops frame scheduling/rendering.
- frame updates redraw only the right video region; no 10 FPS full-screen clear.
- presentation draws no full-width top divider and no vertical separator at the left edge of the video viewport.
- one packed frame buffer = 2646 bytes; no full uncompressed video buffer in RAM.
- no `HttpTransport`, `HTTPClient`, `WiFiClientSecure`, `NetworkArbiter`, AppDataWorker request, or dedicated FreeRTOS task for playback.
- original MP4 and generated C++ media are not committed; generation is deterministic from pinned source identity.
- source-code/project licensing does not relicense the underlying Bad Apple!! PV/music; rights in that media remain with their respective holders.

# Nixie Clock

No remote provider:

```text
common NTP -> ESP32 system clock -> DeviceLayer::localDateTime()
           -> NixieClockModel -> NixieClockScreen
```

Nixie must not call `HttpTransport`, `HTTPClient`, `WiFiClientSecure`, `AppDataWorker` or `NetworkArbiter`. It is the default startup app. There is no global idle timeout or automatic idle destination.

# Home Assistant

## Role

**The user's already-running Home Assistant installation is the server. T-Display-S3 is only a REST API client.**

The board does not run, proxy or emulate a Home Assistant server. Its `:8081` listener is only a local configuration page for this client.

## Entity read

For each configured entity:

```text
GET <existing-ha-base-url>/api/states/<entity_id>
Authorization: Bearer <long-lived-access-token>
Accept: application/json
```

V1 is read-only. `/api/services` writes/control are out of scope.

Retained fields:

```text
entity_id
state
attributes.friendly_name (optional)
attributes.unit_of_measurement (optional)
```

Rules:

- response `entity_id` must match configured entity.
- state required/bounded; optional friendly name/unit bounded.
- parse failure does not mutate previous entity cache.
- 1–4 entities, sequential cycle.
- refresh 30–300 s, default 30 s.
- active-only.

## HTTP mode

`http://` uses ordinary `WiFiClient` and intentionally supports common existing LAN HA installations such as `http://homeassistant.local:8123` or an IP on port 8123.

CA not required. Bearer Token is cleartext to LAN observers, so only use this mode on a trusted LAN.

## HTTPS mode

`https://` requires configured PEM CA:

```text
WiFiClientSecure
setCACert(...)
```

`setInsecure()` is forbidden for this credentialed HA HTTPS path.

Both modes acquire `NetworkArbiter`, retain at most 4 KiB, use normal connect/read/reuse limits, and never log Token/Authorization contents. Diagnostics identify `mode=HA_HTTP` or `mode=HA_CA`.

Unknown URL schemes invalid; HTTPS without valid CA invalid.

# Crypto

V1:

```text
GET https://data-api.binance.vision/api/v3/ticker/24hr
    ?symbols=["BTCUSDT","ETHUSDT","SOLUSDT"]
```

No Binance API credential.

Required fields: `symbol`, `lastPrice`, `priceChangePercent`, `closeTime`.

Parser maps by symbol, rejects duplicate/unknown/missing/malformed values, parses numeric strings completely, and assigns only after all 3 ticker rows validate. Fixed 60 s V1 refresh, active-only; failure preserves last complete snapshot.

# Shared AppDataWorker

Weather / HA / Crypto share exactly one worker:

```text
one request queue -> AppDataWorker
                 -> typed WEATHER / HOME_ASSISTANT / CRYPTO result queues
```

Typed result queues prevent one App consuming/dropping another App's late result. FreeRTOS queues store pointers to C++ request/result objects rather than byte-copying objects containing `std::string`.

An already-running request may finish after app exit, but inactive apps do not schedule follow-up cycles and never redraw TFT. Bad Apple playback is separate local UI work and does not use this worker.

# NetworkArbiter / public HttpTransport

All actual external HTTP/TLS operations serialize through `NetworkArbiter`: max one at once.

Public Stock/Weather/Crypto `HttpTransport` remains: connect 1500 ms, read setting 2500 ms, TLS handshake cap 5 s, retained body max 32 KiB, reuse false. Public-data clients retain existing `setInsecure()` behavior; this does **not** extend to HA HTTPS credentials.

Bad Apple runtime playback does not acquire NetworkArbiter and does not perform network I/O.

# Diagnostics

```text
[md]      QUOTE / INTRADAY / PROBE
[appdata] WEATHER / HOME_ASSISTANT / CRYPTO
[net]     host=... mode=HA_HTTP|HA_CA arb=... io=... total=...
[sys]     MENU|STOCK|WEATHER|NIXIE_CLOCK|HOME_ASSISTANT|CRYPTO|DEVICE_INFO
```

No full provider bodies or credentials by default.

# Cache/error isolation

Weather failure cannot alter Stock health; intraday failure cannot alter quote health; Stock failure cannot clear Weather/HA/Crypto; HA failure preserves per-entity cache; Crypto failure preserves last complete snapshot; Nixie cannot enqueue network work; inactive late result cannot redraw TFT. Weather media playback cannot change provider/network health.

Provider changes require real diagnostics plus regression coverage; do not relax strict parsing merely to raise apparent success rate.
