# Home Assistant + Crypto + Nixie Idle Suite Implementation Plan

**Goal:** Extend the T-Display GP shell with a read-only Home Assistant client, low-frequency Crypto dashboard, Nixie default startup and a global 30-second idle-to-Nixie policy while preserving Stock/Weather behavior.

**Final architecture:** Weather, Home Assistant and Crypto reuse the **single** `AppDataWorker`. Actual external HTTP/TLS operations serialize through `NetworkArbiter`; typed result queues prevent cross-app delayed-result loss. Nixie/DeviceInfo remain local-only. `AppManager` owns startup/idle behavior.

## Final scope

- Hardware: LILYGO T-Display-S3, 320×170 landscape rotation 3.
- Menu: Stock / Weather / Nixie / Home Assistant / Crypto / DeviceInfo.
- **Startup: NixieClock.**
- `MENU`, `WEATHER`, `HOME_ASSISTANT`, `CRYPTO`, `DEVICE_INFO`: 30 s without valid input -> NixieClock.
- `STOCK` and `NIXIE_CLOCK`: idle-exempt.
- valid GPIO0/GPIO14 short/long events reset idle timer; network/data activity does not.
- no additional per-app FreeRTOS worker.
- last valid remote data survives failures/transitions.
- PR remains Draft until physical acceptance.

## Home Assistant V1

Role: **user's existing Home Assistant is the server; T-Display-S3 is only a REST client.**

- read-only; no `/api/services` writes/control actions.
- 1–4 entity IDs with optional labels.
- refresh 30–300 s, default 30 s.
- sequential `GET <base_url>/api/states/<entity_id>`.
- Bearer Long-Lived Access Token.
- HTTP mode supports existing/default trusted-LAN HA installations (commonly port 8123); no CA, Token is cleartext on LAN.
- HTTPS mode requires CA PEM via `WiFiClientSecure::setCACert`; `setInsecure()` is forbidden.
- both modes acquire `NetworkArbiter` and use bounded response retention.
- Token/CA stored in separate `ha_config` NVS blob; AppConfig remains schema v2.
- T-Display config portal: `http://<device-ip>:8081/`; this is not an HA server.
- status exposes only `ha_token_set` / `ha_ca_set`, never secret contents.

## Crypto V1

CoinGecko was dropped after current documentation verification showed key requirements. Final provider is Binance's dedicated market-data-only host:

```text
GET https://data-api.binance.vision/api/v3/ticker/24hr
    ?symbols=["BTCUSDT","ETHUSDT","SOLUSDT"]
```

- no API key.
- BTC / ETH / SOL versus USDT.
- one request obtains all 3 24h tickers.
- latest price + 24h percent.
- fixed V1 cadence 60 s.
- active-only scheduling.
- strict parser maps by symbol, rejects duplicates/missing/malformed values, and updates only after all 3 validate.

## Shared AppDataWorker

- one request queue executes WEATHER / HOME_ASSISTANT / CRYPTO.
- per-type result queues prevent cross-app result loss.
- FreeRTOS queues store pointers to C++ request/result objects; no byte-copy of non-trivial `std::string` objects.
- already-running requests may finish after app exit, but inactive apps schedule no follow-up work and never redraw TFT.

## Nixie startup / idle

`AppManager` owns the policy rather than individual apps.

- default `begin()` and `main.cpp` startup target `AppId::NIXIE_CLOCK`.
- timeout exactly 30000 ms using unsigned wrap-safe elapsed arithmetic.
- timeout switch occurs before old app's next tick, preventing one extra remote request at the boundary.
- Stock stays indefinitely; Nixie stays indefinitely.
- Nixie remains local-only and does not become a background network/scheduler feature.

## TDD / verification checklist

- [x] RED dashboard integration contract verified.
- [x] typed AppDataWorker result routing; Weather adapted.
- [x] HA config validation/codec/store.
- [x] HA Provider/controller/app/UI/config portal.
- [x] HA HTTPS CA transport with secret-redaction contract.
- [x] HA official/default HTTP-server compatibility added via RED -> GREEN.
- [x] Crypto Provider/controller/app/UI.
- [x] CoinGecko replaced with Binance market-only endpoint.
- [x] HA ArduinoJson string regression reproduced/fixed.
- [x] ESP32 `OK` symbol collision reproduced/fixed.
- [x] Nixie default-start and 30-second idle policy added via RED -> GREEN.
- [x] Stock/Nixie idle exemptions and millis wrap tests.
- [x] README / AGENTS / deployment / API / hardware acceptance aligned.
- [ ] exact-head Ubuntu native PASS.
- [ ] exact-head Windows native PASS.
- [ ] exact-head ESP32-S3 firmware build PASS.
- [ ] exact-head Artifact SHA256 independently verified.
- [ ] physical T-Display-S3 acceptance by Codex/user.
