# T-Display GP Deployment

## Responsibility / gates

Web ChatGPT owns source/design/tests/GitHub/CI/ESP32-S3 build/exact-SHA verification. Codex only flashes approved prebuilt application firmware and performs physical testing.

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_app_shell_contract.py
python tools/validate_dashboard_apps_contract.py
python tools/validate_bambu_cloud_contract.py
python tools/validate_bambu_pubsub_timeout_contract.py
python tools/validate_bad_apple_contract.py
pio test -e native
python tools/prepare_bad_apple_asset.py
pio run -e lilygo-t-display-s3
```

Normal flash: verify manifest source/firmware SHA; write only `firmware.bin` at manifest offset (normally `0x10000`); preserve NVS/bootloader/partition table. Serial 115200.

## UI / integrations

Startup Stock; menu exactly `股票 / 天气 / Bambu Lab / 智能家居 / 设备信息`; no auto idle switch.

Trusted-LAN integrations page: `http://<device-ip>:8081/`.

HA is read-only. Bambu is Manual Token only: no password/SMS/email/TFA/automatic renewal. User pastes browser token locally, configures up to four `name + Serial` slots, and chooses active printer. Token must never appear in ChatGPT/Codex prompts, screenshots, serial logs or GitHub.

Bambu routes: status, printers, discover, config, logout. Old login/verify/resend routes remain retired. Discovery is explicit and not persistent until Save.

On-device active selection:

```text
GPIO0 short  previous printer
GPIO14 short next printer
```

Selection wraps and persists. It does **not** tear down MQTT when the configured printer set/credentials/region are unchanged.

## Bambu MQTT runtime

Platform: `espressif32@6.12.0`, Arduino-ESP32 2.0.17, PubSubClient 2.8. Runtime is loop-driven and adapted from `Keralots/BambuHelper` (MIT).

Each configured printer has a persistent runtime slot:

```text
slot[n]
  WiFiClientSecure (strict CA, timeout 15 s)
  PubSubClient (40960 buffer, keepalive 30 s)
  own reconnect/backoff state
  own delayed pushall state
  own BambuState cache
```

Per-slot Cloud path:

```text
cn.mqtt.bambulab.com:8883 or us.mqtt.bambulab.com:8883
username = cloudUserId
password = accessToken
subscribe device/<slot serial>/report
request   device/<slot serial>/request
```

`process(nowMs)` services established slots first and starts at most one blocking connection attempt per loop pass, preferring the active slot. A failed/stale slot destroys/rebuilds only its own clients. Backoff per slot: 30 s → 60 s after 5 failures → 120 s after 15 failures. Initial read-only `pushall` is sent >=2 s after that slot connects.

Active-index/name-only changes preserve sockets and caches. Credential, region, enablement, printer count or serial-set changes are connection-affecting and cause runtime rebuild. One slot dropping must not disconnect another online slot.

No dedicated CPU0 `bambu-mqtt` task, no `setInsecure()`, no watchdog disable/delete, no explicit production TLS preflight, no `mqtt_real_tls_*`, no project `MQTT_SOCKET_TIMEOUT=3/5` override. `esp_task_wdt_reset()` around known long operations is allowed to match upstream behavior.

Secret-safe logs include `slot=<n>`:

```text
[bambu] mqtt_connect slot=...
[bambu] mqtt_connect_ok slot=... elapsed_ms=...
[bambu] mqtt_connect_fail slot=... rc=... retry_ms=...
[bambu] mqtt_subscribe_fail slot=...
[bambu] mqtt_loop_lost slot=...
[bambu] mqtt_pushall_initial slot=...
```

Never log Token, Cloud User ID, Cookie/Authorization or auth payloads.

## Physical smoke

1. Flash exact-head application at `0x10000`, preserve NVS.
2. Verify normal UI plus Bambu anti-flicker regression.
3. With two saved printers, wait until both slot 0 and slot 1 have `mqtt_connect_ok` and initial pushall/report state.
4. Perform >=10 A↔B switches. Switch itself must not emit a fresh `mqtt_connect` when both slots are already online; display should change on the next normal render cadence rather than waiting for TLS/MQTT/pushall.
5. Verify correct per-printer cached data and no cross-contamination.
6. Leave selected printer on B, reboot without erase, confirm B remains selected after boot.
7. Observe >=10 minutes: 0 watchdog, 0 panic, 0 automatic reboot, heap not monotonically declining.
8. If one slot naturally drops, confirm only that slot reconnects while the sibling remains alive.
9. Test Stock/Weather/HA coexistence because two persistent TLS/MQTT slots increase resource pressure.
10. Code supports up to 4 configured slots, but do not claim 4 simultaneous hardware acceptance until separately tested.

See `docs/hardware-acceptance.md` for the full checklist.
