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
python tools/validate_bambu_pubsub_timeout_contract.py
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

Single trusted-LAN server:

```text
http://<device-ip>:8081/
```

### Home Assistant

Read-only client of existing HA. Configure Base URL, token, 1–4 entities, refresh and optional HTTPS CA. No secret echo.

### Bambu Lab Cloud — Manual Token

Firmware does not perform Bambu password login/SMS/email/TFA/automatic renewal.

1. Log in to the correct Bambu website in a desktop browser.
2. Obtain the browser `token` from developer tools/Cookies.
3. Paste it into Bambu Access Token on `:8081`.
4. Fill up to four local printer slots (`name + Serial`) and select the active printer; or explicitly use “用 Token 获取我的打印机” to fill the browser form.
5. Click “保存并切换”.

Token is a credential. Do not put it in ChatGPT/Codex prompts, screenshots, serial logs or GitHub. Blank Token on Save preserves the stored Token.

Bambu routes:

```text
GET  /api/bambu/status
GET  /api/bambu/printers
POST /api/bambu/discover
POST /api/bambu/config
POST /api/bambu/logout
```

Old login/verify/resend routes stay retired. Discovery is explicit-only and browser-side until Save. `/api/bambu/printers` is local saved data only.

Active printer can also be changed on-device:

```text
GPIO0 short  previous printer
GPIO14 short next printer
```

Selection wraps, persists, clears old state and reconnects without reboot/re-authentication. Bambu rendering is incremental; routine live data must not cause whole-screen flashing.

## Bambu MQTT runtime

The production runtime is aligned to `Keralots/BambuHelper`'s proven Cloud MQTT lifecycle (MIT), while retaining this project's Manual Token/config/UI boundaries.

Platform:

```text
PlatformIO espressif32@6.12.0
Arduino-ESP32 2.0.17
PubSubClient 2.8
```

Cloud connection:

```text
China  cn.mqtt.bambulab.com:8883
Global us.mqtt.bambulab.com:8883
username = cloudUserId
password = accessToken
subscribe device/<activeSerial>/report
```

Runtime sequence:

```text
Arduino loop calls BambuMqttService::process(nowMs)
 -> discard stale MQTT/TLS objects before reconnect
 -> WiFiClientSecure + strict CA bundle + timeout 15 s
 -> PubSubClient + buffer 40960 + keepalive 30 s
 -> random bblp_* client ID
 -> PubSubClient owns TCP/TLS + MQTT CONNECT
 -> subscribe report topic
 -> release NetworkArbiter
 -> >=2000 ms after connect, one read-only pushall
```

There is no dedicated `bambu-mqtt` task pinned to CPU0. The old Stage-2/3/4 production experiments are retired: no normal-path layered DNS/TCP/TLS preflight, no explicit `tls_->connect(...)` before PubSubClient, and no project `MQTT_SOCKET_TIMEOUT=3/5` override.

Cloud reconnect backoff:

```text
failures 0..4   -> 30 s
failures 5..14  -> 60 s
failures >=15   -> 120 s
```

rc 4/5 sets `token_invalid` and suppresses retries with that config until a config revision changes. Other connect/subscribe/loop failures fully release clients and retry under backoff.

The reference implementation resets Task WDT around known long operations; this adaptation may call `esp_task_wdt_reset()` before MQTT connect/callback/pushall, but never disables/deletes watchdogs. Bambu Cloud TLS remains strict CA; `setInsecure()` is forbidden.

Secret-safe serial markers:

```text
[bambu] mqtt_connect broker=<host> ... fails=<n>
[bambu] mqtt_connect_ok elapsed_ms=<ms> ...
[bambu] mqtt_connect_fail rc=<n> elapsed_ms=<ms> ... retry_ms=<ms>
[bambu] mqtt_subscribe_fail ...
[bambu] mqtt_loop_lost ...
[bambu] mqtt_pushall_initial seq=<n> delay_ms=<ms>
```

Never log Token, Cloud User ID, Cookie/Authorization or auth payloads.

## Physical smoke

1. Flash exact-head application image at `0x10000`, preserve NVS.
2. Boot Stock; verify five apps/no idle switch.
3. Verify Weather/Bad Apple and HA regression.
4. Confirm Manual Token UI/four printer slots and no secret echo.
5. With a valid saved printer, observe Bambu MQTT for >=10 minutes: no watchdog/panic/automatic reboot.
6. Confirm `mqtt_connect_ok`, report/live-state flow and delayed initial pushall when broker accepts connection.
7. If connect fails, confirm it returns cleanly and follows 30/60/120 s backoff rather than reboot/churn.
8. Device-switch A→B→A: no reboot/login/Token prompt, old state clears, selected index persists, new Serial reconnects.
9. Observe Bambu UI >=2 min: no periodic whole-screen flash.
10. Leave Bambu for Stock/Weather/HA and return; MQTT/background state remains fresh and other network features remain usable.
11. Reboot without erase; Token/printer list/active selection restore.
12. Safe Wi-Fi interruption/recovery: reconnect without panic/watchdog/heap leak.

See `docs/hardware-acceptance.md` for the full checklist.
