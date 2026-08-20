# Home Assistant + Crypto + Nixie Idle Suite Implementation Plan

**Goal:** Extend T-Display GP with a read-only Home Assistant client, low-frequency Crypto dashboard, Nixie default startup and global 30-second idle-to-Nixie behavior while preserving Stock/Weather.

## Final architecture

- Weather / Home Assistant / Crypto reuse exactly one `AppDataWorker`.
- Actual external HTTP/TLS serializes through `NetworkArbiter`.
- Results are separated by `AppDataRequestType` to prevent delayed cross-app loss.
- Nixie / DeviceInfo remain local-only.
- `AppManager` owns startup/idle behavior.

## Final scope

```text
Stock / Weather / Nixie / Home Assistant / Crypto / DeviceInfo
```

- startup: `NIXIE_CLOCK`.
- Menu/Weather/HA/Crypto/DeviceInfo: 30 s without valid input -> Nixie.
- Stock/Nixie: idle-exempt.
- valid GPIO0/GPIO14 short/long resets idle timer; network/data activity does not.
- no per-app FreeRTOS worker additions.
- last valid remote data survives errors/transitions.
- Draft PR until real-board acceptance.

## Home Assistant V1

**User's existing Home Assistant is the server. T-Display-S3 is only a REST client.**

- read-only only; no `/api/services` writes.
- 1–4 entity IDs, optional labels.
- refresh 30–300 s, default 30 s.
- sequential `GET <base_url>/api/states/<entity_id>` with Bearer Long-Lived Access Token.
- HTTP mode: compatible with trusted-LAN existing HA (typically port 8123), CA not required; token is cleartext on LAN.
- HTTPS mode: CA PEM required; `WiFiClientSecure::setCACert`, never `setInsecure()`.
- both modes use `NetworkArbiter`, bounded response retention and secret-safe logging.
- Token/CA in separate `ha_config` NVS blob; AppConfig remains schema v2.
- T-Display config UI: `http://<device-ip>:8081/`; not an HA server.
- status returns only `ha_token_set` / `ha_ca_set` booleans.

## Crypto V1

Final provider: Binance market-data-only:

```text
GET https://data-api.binance.vision/api/v3/ticker/24hr
    ?symbols=["BTCUSDT","ETHUSDT","SOLUSDT"]
```

- no API key.
- BTC / ETH / SOL versus USDT.
- one request for three tickers.
- price + 24h percent.
- fixed 60 s V1 refresh.
- active-only scheduling.
- strict symbol-mapped fail-closed parser; whole snapshot updates only after all 3 validate.

## TDD / implementation checklist

- [x] RED dashboard integration contract.
- [x] typed AppDataWorker result routing and Weather adaptation.
- [x] HA config validation/codec/store.
- [x] HA Provider/controller/app/UI/config portal.
- [x] HA HTTPS CA security and secret redaction.
- [x] official/default HA HTTP compatibility via RED -> GREEN.
- [x] Crypto Provider/controller/app/UI.
- [x] CoinGecko replaced by Binance market-only endpoint after current-doc verification.
- [x] HA ArduinoJson string regression reproduced/fixed.
- [x] ESP32 `OK` symbol collision reproduced/fixed.
- [x] Nixie default startup and 30-second idle policy via RED -> GREEN.
- [x] Stock/Nixie idle exemptions and millis wrap tests.
- [x] README / AGENTS / deployment / API / hardware acceptance aligned.
- [ ] exact-head Ubuntu native PASS.
- [ ] exact-head Windows native PASS.
- [ ] exact-head ESP32-S3 build PASS.
- [ ] exact-head Artifact SHA256 independently verified.
- [ ] physical T-Display-S3 acceptance by Codex/user.
