# T-Display GP Deployment

## Responsibility split

Web ChatGPT owns source/design/tests/GitHub/CI/ESP32-S3 build/exact-SHA artifact verification. Codex only flashes approved prebuilt firmware and performs physical testing.

## Development gates

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_app_shell_contract.py
python tools/validate_dashboard_apps_contract.py
python tools/validate_bambu_cloud_contract.py
python tools/validate_bad_apple_contract.py
pio test -e native
python tools/prepare_bad_apple_asset.py
pio run -e lilygo-t-display-s3
```

CI also runs Windows native and publishes `tdisplay-gp-firmware-<SOURCE_SHA>`.

## Flash

Verify manifest `source_sha` and `firmware_sha256`. Normal upgrade writes only `firmware.bin` at manifest `firmware_offset` (normally `0x10000`). Do not erase NVS or rewrite bootloader/partition table. Serial 115200.

## Expected boot/UI

Startup Stock. Menu exactly:

```text
股票 / 天气 / Bambu Lab / 智能家居 / 设备信息
```

No automatic idle switching.

## Integrations page

Single server:

```text
http://<device-ip>:8081/
```

Trusted-LAN HTTP only.

### Home Assistant

Read-only client of existing HA. Configure Base URL, token, 1–4 entities, refresh and optional HTTPS CA. No secret echo.

### Bambu Lab Cloud — Manual Token

The firmware no longer logs into a Bambu account and no longer performs SMS/email/TFA verification or automatic password/token renewal.

Setup:

1. Log in to the correct Bambu website in a desktop browser.
2. Use developer tools/Cookies to obtain the `token` value.
3. Paste it into the Bambu **Access Token** field on `:8081`.
4. Fill up to four local printer slots (`name + Serial`) and choose the active printer; or explicitly click “用 Token 获取我的打印机” to populate the form from Cloud.
5. Click “保存并切换”.

Token is a credential. Do not put it in ChatGPT/Codex prompts, screenshots, serial logs or GitHub.

“用 Token 获取我的打印机” is explicit only. Discovery results remain browser-side until Save. `/api/bambu/printers` reads local persisted printers only and does not call Cloud.

Bambu routes:

```text
GET  /api/bambu/status
GET  /api/bambu/printers
POST /api/bambu/discover
POST /api/bambu/config
POST /api/bambu/logout
```

Old account-login/verify/resend routes are retired.

Saved config supports 4 printers. Active-printer change applies immediately without reboot or re-authentication. Service disconnects old MQTT, clears the old printer snapshot, then connects/subscribes with the newly selected Serial.

The same saved list can be switched directly from the device while the Bambu app is visible:

```text
GPIO0 short  previous printer
GPIO14 short next printer
```

Selection wraps A ↔ B ↔ …, does nothing when fewer than two printers are saved, persists `activePrinterIndex` immediately, clears the old state and reconnects. GPIO0 long still returns to the menu; GPIO14 long remains no-op.

Bambu rendering is incremental. A service-cache poll does not itself trigger a redraw. Full-screen clear occurs only for a full redraw such as entering Bambu or switching active printer; normal live data changes redraw only their header/progress/job/filament/footer regions. This is the required anti-flicker behavior.

Brokers:

```text
China  cn.mqtt.bambulab.com:8883
Global us.mqtt.bambulab.com:8883
```

MQTT username = Cloud User ID, password = Access Token. Only read-only `pushall` request publish is allowed. MQTT receive buffer = 40960 bytes.

If the Token is rejected with MQTT rc 4/5, service enters `token_invalid` and stops retrying that same Token. Obtain a fresh browser Token and save it in `:8081`; changing config revision resumes MQTT.

Legacy Bambu NVS schema v1 is migrated to v2 when possible. Reusable Token/User ID/single printer are retained; account/password are dropped.

### Layered MQTT/TLS diagnostic firmware

When the Cloud MQTT problem cannot be localized from `rc=-2` / numeric TLS error alone, the diagnostic build performs one bounded preflight inside the same `NetworkArbiter` critical section before the real MQTT CONNECT:

```text
DNS resolve broker
plain TCP connect broker:8883
strict-CA TLS preflight broker:8883
real PubSubClient MQTT CONNECT
```

Serial 115200 should show:

```text
[netcfg] ip=<...> mask=<...> gateway=<...> dns1=<...> dns2=<...> bssid=<...> ch=<...> wifi=<...>
[bambu] mqtt_diag_heap phase=before_probe internal_free=<...> internal_largest=<...> dma_free=<...> heap_free=<...>
[bambu] mqtt_diag_dns ok=<0|1> ip=<resolved-ip> elapsed_ms=<...>
[bambu] mqtt_diag_tcp ok=<0|1> ip=<resolved-ip> port=8883 elapsed_ms=<...>
[bambu] mqtt_diag_tls ok=<0|1> tls=<numeric> elapsed_ms=<...>
[bambu] mqtt_diag_heap phase=after_probe ...
[bambu] mqtt_diag_heap phase=after_mqtt_buffer ...
[bambu] mqtt_connect_ok elapsed_ms=<...> ...
[bambu] mqtt_connect_fail rc=<mqtt> tls=<numeric> elapsed_ms=<...> internal_free=<...> internal_largest=<...> dma_free=<...>
```

Interpretation:

```text
DNS fail                         -> resolver/network config layer
DNS pass + TCP fail             -> route/socket/TCP layer
TCP pass + strict TLS fail      -> TLS/CA/handshake/resource layer
TLS preflight pass + real rc=-2 -> compare after_mqtt_buffer heap/largest block and real-connect elapsed
TLS pass + rc 4/5               -> MQTT authentication / Token layer
```

The probes are diagnostic instrumentation only and intentionally create extra short-lived TCP/TLS connections during reconnect attempts. They are not a permanent optimization or fallback. Do not use them to justify disabling CA, changing Token handling or adding new publishes. Keep MQTT keepalive = 30 s and reconnect cadence = 30 s unless later physical evidence supports a separate policy change.

Never include Access Token, Cloud User ID, Cookie/Authorization, account data, auth payloads or raw credential-bearing TLS error text in logs. Numeric errors, broker/IP, elapsed time and heap metrics are allowed.

## Physical smoke

1. Flash exact-SHA application image, preserve NVS.
2. Boot Stock; verify five apps/no idle switch.
3. Weather/Bad Apple and HA regression.
4. Open `:8081`; confirm Manual Token UI and four printer slots.
5. Paste Token locally, configure two printers and select printer A; Save.
6. Confirm MQTT online and printer A state.
7. In the Bambu app short-press GPIO14; confirm device switches A→B, clears A state, persists B and reconnects without reboot/login.
8. Short-press GPIO0; confirm B→A wrap/persistence/reconnect. With only one saved printer, both short presses must be no-op.
9. Observe Bambu screen while idle and during live updates; it must not flash the entire display on a 500 ms cadence.
10. Return to `:8081`, change active selection and Save; web switching must still work without reboot/login.
11. Optionally test explicit discovery once; verify it populates form but does not overwrite saved config until Save.
12. Reboot with NVS preserved; saved Token/list/active printer should restore.
13. Leave Bambu app and verify background freshness while Stock/Weather/HA remain usable.
14. For any MQTT failure, preserve one complete `[netcfg]` + `mqtt_diag_heap/dns/tcp/tls` + real `mqtt_connect_*` sequence. Correlate it with `/api/bambu/status` and, where available, firewall/PC-side evidence before changing policy.
15. Wi-Fi loss/recovery must reconnect without panic/watchdog/heap leak.
16. Token-invalid test only if safe; do not intentionally lock the account.

See `docs/hardware-acceptance.md`.