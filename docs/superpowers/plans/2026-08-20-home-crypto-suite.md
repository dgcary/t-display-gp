# Home Assistant + Crypto Suite Implementation Plan

**Goal:** Extend the T-Display GP shell with a secure read-only Home Assistant dashboard and a low-frequency cryptocurrency dashboard while preserving Stock, Weather, Nixie and DeviceInfo behavior.

**Final architecture:** Weather, Home Assistant and Crypto reuse the **single** `AppDataWorker`. Requests share one worker task and external TLS remains serialized through `NetworkArbiter`; results are separated by `AppDataRequestType` so one app cannot consume another app's delayed result. Local-only Nixie/DeviceInfo remain outside the worker.

## Final scope

- Hardware remains LILYGO T-Display-S3, 320×170 landscape rotation 3.
- Startup remains StockApp; existing GPIO semantics are unchanged.
- Menu: Stock / Weather / Nixie / Home Assistant / Crypto / DeviceInfo.
- No additional per-app FreeRTOS worker.
- Last valid data survives request failure and app transitions.
- Home Assistant and Crypto schedule only while active.
- PR stays Draft until real-board acceptance.

## Home Assistant V1

- Read-only only; no `/api/services` writes or control actions.
- 1–4 configured entities with optional display labels.
- Refresh 30–300 seconds, default 30 seconds.
- Sequential `GET <base_url>/api/states/<entity_id>` requests.
- `Authorization: Bearer <long-lived access token>`.
- Device→HA TLS is strict: configured CA PEM is passed to `WiFiClientSecure::setCACert`; `setInsecure()` is forbidden for this credentialed path.
- Token and CA are stored in a dedicated `ha_config` NVS blob under the existing namespace and are not added to AppConfig schema v2.
- The existing AppConfig schema therefore remains **v2**; no stock/weather migration change is required.
- HA has a separate LAN configuration portal at `http://<device-ip>:8081/`.
- That local portal is plain HTTP and must be used only on a trusted LAN. It never returns Token/CA contents; status exposes only `ha_token_set` / `ha_ca_set` booleans.
- Blank Token/CA fields preserve existing secrets; save validates atomically and reboots.

## Crypto V1

The original CoinGecko idea was dropped after current documentation verification showed Demo/Pro API keys are now required.

Final provider: Binance's dedicated market-data-only host:

```text
GET https://data-api.binance.vision/api/v3/ticker/24hr
    ?symbols=["BTCUSDT","ETHUSDT","SOLUSDT"]
```

The request is URL-encoded in firmware. Binance documents `data-api.binance.vision` as public market-data-only and requiring no authentication/API key.

- Assets: BTC, ETH, SOL versus USDT.
- One request obtains all three 24h tickers.
- Display: latest price + 24h percent change.
- Refresh default/fixed V1 cadence: 60 seconds.
- Strict parser maps symbols independent of response order, rejects duplicates/missing/malformed values, and updates cache only after the whole three-symbol response validates.

## Shared worker/result routing

- One request queue executes WEATHER / HOME_ASSISTANT / CRYPTO.
- Typed result queues prevent cross-app result loss.
- FreeRTOS queues store pointers to C++ request/result objects; do not byte-copy non-trivial objects containing `std::string`.
- An external request already executing may finish after app exit, but inactive apps do not schedule follow-up work. Typed late results wait until that app is active again.

## TDD / verification checklist

- [x] RED dashboard integration contract verified before implementation.
- [x] Typed app-data routing implemented; Weather adapted.
- [x] Home Assistant configuration validation/codec/store implemented.
- [x] Home Assistant strict-TLS provider implemented.
- [x] Home Assistant controller/app/UI/config portal implemented.
- [x] Crypto provider/controller/app/UI implemented.
- [x] CoinGecko replaced with Binance market-data-only endpoint after current-doc verification.
- [x] Provider/controller/config native tests added.
- [x] HA ArduinoJson string regression reproduced and fixed.
- [ ] Final documentation alignment.
- [ ] Exact-head Ubuntu native PASS.
- [ ] Exact-head Windows native PASS.
- [ ] Exact-head ESP32-S3 firmware build PASS.
- [ ] Exact-head artifact hashes independently verified.
- [ ] Physical T-Display-S3 acceptance by Codex/user.
