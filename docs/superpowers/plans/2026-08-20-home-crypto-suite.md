# Home Assistant + Crypto + Nixie Idle Suite — Final Plan

Scope is frozen pending exact-head verification and physical acceptance.

## Implemented behavior

- six App shell: Stock / Weather / Nixie / Home Assistant / Crypto / DeviceInfo.
- startup defaults to NixieClock.
- Menu/Weather/HA/Crypto/DeviceInfo idle to Nixie after 30000 ms without valid button input.
- Stock and Nixie are idle-exempt; valid GPIO short/long resets timer; network activity does not.
- Weather/HA/Crypto share exactly one AppDataWorker with typed result queues.
- Home Assistant uses the user's existing HA server; T-Display is read-only REST client.
- HA HTTP supports trusted-LAN existing servers without CA; HTTPS requires CA via setCACert and forbids setInsecure.
- HA Token/CA redacted from status/logs; config is separate `ha_config` NVS blob.
- Crypto uses Binance market-data-only `data-api.binance.vision`, BTCUSDT/ETHUSDT/SOLUSDT, no API key, 60 s active-only refresh.
- Nixie remains local-only and partial-refresh.
- Stock Tencent/EastMoney QoS/failover semantics remain unchanged.

## Verification history / TDD

- [x] dashboard integration RED -> GREEN.
- [x] typed AppDataWorker routing.
- [x] HA provider/controller/config/UI/portal.
- [x] HA JSON string regression reproduced and fixed.
- [x] ESP32 `OK` symbol collision reproduced and fixed.
- [x] Crypto provider/controller/UI; CoinGecko dropped for Binance market-only after current-doc verification.
- [x] Nixie default startup / 30 s idle RED -> GREEN.
- [x] Stock/Nixie idle exemptions, activity reset and millis-wrap tests.
- [x] HA default HTTP-server compatibility RED -> production HTTP/HTTPS dual mode.
- [x] README / AGENTS / deployment / API / hardware acceptance synchronized.

## Remaining gates

- [ ] exact-head validators PASS.
- [ ] exact-head Ubuntu native PASS.
- [ ] exact-head Windows native PASS.
- [ ] exact-head ESP32-S3 build PASS.
- [ ] exact-head Artifact manifest and SHA256 independently verified.
- [ ] physical T-Display-S3 acceptance by Codex/user.

No additional feature work is allowed before these gates close unless a verification failure requires a corrective patch.
