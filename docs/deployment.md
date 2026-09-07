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

Brokers:

```text
China  cn.mqtt.bambulab.com:8883
Global us.mqtt.bambulab.com:8883
```

MQTT username = Cloud User ID, password = Access Token. Only read-only `pushall` request publish is allowed. MQTT receive buffer = 40960 bytes.

If the Token is rejected with MQTT rc 4/5, service enters `token_invalid` and stops retrying that same Token. Obtain a fresh browser Token and save it in `:8081`; changing config revision resumes MQTT.

Legacy Bambu NVS schema v1 is migrated to v2 when possible. Reusable Token/User ID/single printer are retained; account/password are dropped.

## Physical smoke

1. Flash exact-SHA application image, preserve NVS.
2. Boot Stock; verify five apps/no idle switch.
3. Weather/Bad Apple and HA regression.
4. Open `:8081`; confirm Manual Token UI and four printer slots.
5. Paste Token locally, configure two printers and select printer A; Save.
6. Confirm MQTT online and printer A state.
7. Change active selection to printer B, Save; no reboot/login should be required. Confirm old state disappears and B connects to printer B.
8. Switch back to A and confirm the same behavior.
9. Optionally test explicit discovery once; verify it populates form but does not overwrite saved config until Save.
10. Reboot with NVS preserved; saved Token/list/active printer should restore.
11. Leave Bambu app and verify background freshness while Stock/Weather/HA remain usable.
12. Wi-Fi loss/recovery must reconnect without panic/watchdog/heap leak.
13. Token-invalid test only if safe; do not intentionally lock the account.

See `docs/hardware-acceptance.md`.
