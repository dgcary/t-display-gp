# T-Display GP Deployment

## Responsibility / gates

Web ChatGPT owns source/design/tests/GitHub/CI/ESP32-S3 build/exact-SHA verification. Codex only flashes approved prebuilt application firmware and performs physical testing.

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_app_shell_contract.py
python tools/validate_direct_navigation_contract.py
python tools/validate_dashboard_apps_contract.py
python tools/validate_bambu_cloud_contract.py
python tools/validate_bambu_pubsub_timeout_contract.py
python tools/validate_bad_apple_contract.py
pio test -e native
python tools/prepare_bad_apple_asset.py
pio run -e lilygo-t-display-s3
```

Normal flash: verify manifest source/firmware SHA; write only `firmware.bin` at manifest offset (normally `0x10000`); preserve NVS/bootloader/partition table. Serial 115200.

## Direct page UI / integrations

Runtime menu is removed. Startup is Weather. Button behavior:

```text
GPIO0 short  previous page
GPIO14 short next page
GPIO0 long   no-op
GPIO14 long  no-op
```

The flattened page order is:

```text
Weather
-> Stock1 -> Stock2 -> Stock3 -> Stock4
-> Bambu1 -> Bambu2
-> Home Assistant
-> Device Info
-> Weather
```

Only configured pages are present. Direct navigation exposes at most the first 4 configured stocks and first 2 configured Bambu printers; missing stock/printer slots are skipped automatically. Same-app page changes stay inside that app and do not exit/re-enter it. Configuration is still edited through the device Web portals rather than buttons.

Stock/weather configuration: `http://<device-ip>/`.
Trusted-LAN integrations page for HA/Bambu: `http://<device-ip>:8081/`.

HA is read-only. Bambu is Manual Token only: no password/SMS/email/TFA/automatic renewal. User pastes browser token locally. Token must never appear in ChatGPT/Codex prompts, screenshots, serial logs or GitHub.

Bambu routes: status, printers, discover, config, logout. Old login/verify/resend routes remain retired. Discovery is explicit and not persistent until Save.

Selecting Bambu page 1/2 changes only the active printer index. If the configured printer set/credentials/region are unchanged, it must **not** tear down persistent MQTT/TLS slots. The active index remains persisted through the existing Bambu config path.

## Bambu MQTT runtime

Platform: `espressif32@6.12.0`, Arduino-ESP32 2.0.17, PubSubClient 2.8. Runtime keeps the BambuHelper-aligned per-printer persistent-slot model but runs socket work on a dedicated background FreeRTOS worker so Cloud connection stalls cannot freeze the Arduino UI loop.

Each configured printer has a persistent runtime slot:

```text
slot[n]
  WiFiClientSecure (strict CA)
  TCP/socket timeout 5 s
  TLS handshake timeout 5 s
  PubSubClient socket timeout 5 s
  PubSubClient buffer 40960
  keepalive 30 s
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

`begin()` starts one CPU0 priority-1 `bambu-mqtt` worker with an 8192-byte stack. Arduino `loop()` does not call `BambuMqttService::process()` and must remain responsive while the worker is inside a failed Cloud connection attempt.

The worker services established slots first and starts at most one blocking connection attempt per worker pass, preferring the active slot. A failed/stale slot destroys/rebuilds only its own clients. Backoff per slot: 30 s → 60 s after 5 failures → 120 s after 15 failures, anchored from the time the failed connect returns. Initial read-only `pushall` is sent >=2 s after that slot connects.

Active-index/name-only changes preserve sockets and caches. Credential, region, enablement, printer count or serial-set changes are connection-affecting and cause runtime rebuild. One slot dropping must not disconnect another online slot.

The shared network arbiter remains to serialize expensive TLS starts, but each Bambu TCP/TLS/MQTT phase is explicitly bounded. A failed attempt must not reproduce the old ~120 s synchronous freeze. Physical acceptance target for logged `mqtt_connect_fail elapsed_ms` is <=20 s, and local GPIO navigation plus `:8081/api/bambu/status` must stay responsive while the worker is attempting Cloud connect.

No `setInsecure()`, no watchdog disable/delete, no explicit production TLS preflight, no `mqtt_real_tls_*`, no project `MQTT_SOCKET_TIMEOUT` macro override.

Secret-safe logs include `slot=<n>`:

```text
[bambu] mqtt_connect slot=...
[bambu] mqtt_connect_ok slot=... elapsed_ms=...
[bambu] mqtt_connect_fail slot=... rc=... elapsed_ms=... retry_ms=...
[bambu] mqtt_subscribe_fail slot=... retry_ms=...
[bambu] mqtt_loop_lost slot=... retry_ms=...
[bambu] mqtt_pushall_initial slot=...
```

Never log Token, Cloud User ID, Cookie/Authorization or auth payloads.

## Physical smoke

1. Flash exact-head application at `0x10000`, preserve NVS.
2. Verify startup lands directly on Weather and no menu appears.
3. Walk forward and backward through the exact configured page chain. If only 3 stocks exist, Stock4 must be absent; if only 1 Bambu printer exists, Bambu2 must be absent.
4. Verify both long presses are no-op.
5. While Bambu Cloud is unreachable or failing, verify GPIO navigation remains responsive and `http://<device-ip>:8081/api/bambu/status` still answers during the connection attempt.
6. On a forced/observed connect failure, verify `mqtt_connect_fail elapsed_ms <= 20000` and the next `mqtt_connect` starts only after the logged 30/60/120 s retry interval, not immediately.
7. With two saved printers, wait until both slot 0 and slot 1 have `mqtt_connect_ok` and initial pushall/report state.
8. Move Bambu1 -> Bambu2 -> Bambu1 repeatedly. Navigation itself must not emit a fresh `mqtt_connect` when both slots were already online; target cached data should appear on the next UI cadence.
9. Verify correct per-printer cached data and no cross-contamination.
10. Observe >=10 minutes: 0 watchdog, 0 panic, 0 automatic reboot, heap not monotonically declining.
11. If one slot naturally drops, confirm only that slot reconnects while the sibling remains alive.
12. Test Stock/Weather/HA coexistence because persistent TLS/MQTT slots increase resource pressure.

Bambu configuration can still contain up to 4 printers, but direct navigation intentionally exposes only the first 2. Stock configuration may contain the existing supported count, but direct navigation intentionally exposes only the first 4.

See `docs/hardware-acceptance.md` for the full checklist.
