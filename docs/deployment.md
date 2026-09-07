# T-Display GP Deployment

## Fixed responsibility split

**Web ChatGPT:** GitHub source/design/code/tests, validators, native tests, Bad Apple asset generation, real ESP32-S3 PlatformIO compile, exact-SHA Artifact verification and Draft PR maintenance.

**Codex:** exact-SHA Artifact download/hash check, flash prebuilt application, serial monitor, physical UI/network/Bambu Cloud/navigation/soak testing and evidence return. Codex does not use a local rebuild as the normal deployment gate.

## Development checks

Bad Apple generation requires ffmpeg.

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

CI additionally runs Windows native and uploads `tdisplay-gp-firmware-<SOURCE_SHA>` containing `firmware.bin`, `partitions.bin`, `bootloader.bin` and `firmware-manifest.txt`.

## Flash

Codex verifies:

```text
manifest source_sha == approved SHA
actual firmware.bin SHA256 == manifest firmware_sha256
```

Normal upgrade writes only `firmware.bin` at manifest `firmware_offset` (currently normally `0x10000`). Do not erase NVS or rewrite bootloader/partitions. Serial 115200.

Bad Apple media is compiled into firmware.bin; there is no separate media/filesystem flash.

## Expected firmware behavior

Startup: **Stock**.

Menu:

```text
股票 / 天气 / Bambu Lab / 智能家居 / 设备信息
```

No automatic idle switch. Each app/menu remains active until explicit button navigation.

## Weather / Bad Apple

Visible Weather layout:

```text
left: current weather + compact 今/明
right: x=152..319, y=27..152, Bad Apple 168×126
```

No day-after forecast is rendered. No full-width top divider and no vertical separator beside the video. Bad Apple is 2190 frames at 10 FPS (~219 s), silent, looping, local flash only. Entering Weather restarts at frame 0; leaving stops video refresh. Frame cadence redraws only the video viewport.

## Integrations page

HA and Bambu share exactly one local configuration server:

```text
http://<device-ip>:8081/
```

This page is local HTTP and should be used only on a trusted LAN.

### Home Assistant

T-Display remains a read-only client of the user's existing HA server. Configure Base URL, Long-Lived Access Token, 1–4 entity IDs, labels, refresh interval and CA only for HTTPS.

HA HTTP sends Bearer Token in cleartext on the LAN; HTTPS requires strict CA verification. Do not expose HA Token/CA contents in status or serial output.

### Bambu Lab Cloud

Configure locally on the same Integrations page:

- region: China or US/EU/Global;
- account identifier: **China accepts a mainland mobile number (11 digits, optional `+86`/`86` prefix) or email; US/EU/Global uses email**;
- password;
- remember-password choice;
- bound printer selection after successful login/verification.

The firmware keeps the historical config/form key `email` for NVS compatibility, but China-region password login treats it as a generic account identifier and submits it to Bambu Cloud as JSON `account`.

**Do not paste the Bambu password, access token, verification code or authenticator challenge material into ChatGPT/Codex prompts, serial captures or screenshots.** Enter password and any one-time verification code directly in the local page. The firmware must never return raw secrets from status endpoints or print them to serial.

Cloud endpoint behavior:

```text
HTTPS password login: POST /v1/user-service/user/login {account,password}
HTTPS verification: typed SMS / email / TFA recovery when Cloud challenges
HTTPS profile/device discovery: CA verified
China MQTT: cn.mqtt.bambulab.com:8883
US/EU MQTT: us.mqtt.bambulab.com:8883
subscribe: device/<serial>/report
```

If password login succeeds without a challenge, printer discovery continues normally. If Bambu Cloud requires SMS, email or authenticator verification, the service enters `VERIFICATION_REQUIRED` and the same `:8081` page shows the verification box. Submit the one-time code there; after success the replacement access token is persisted and the background service resumes user-ID resolution, printer discovery and MQTT connection.

SMS/email resend is manual only through the page and has a 60-second local cooldown. Authenticator TFA has no resend action. Challenge material such as `tfaKey` remains RAM-only inside the service and is never saved to NVS or exposed through status APIs.

The service may send the single read-only `pushall` state-sync request to `device/<serial>/request`; it must not publish printer-control commands.

MQTT receive buffer is 40960 bytes. Buffer allocation failure is a recoverable visible error, not a reboot condition.

Bambu MQTT remains connected in the background when the user leaves the Bambu app. Connect/reconnect handshakes acquire `NetworkArbiter`; after success the persistent socket releases arbiter and continues independently so Stock/Weather/HA remain usable.

`BambuMqttService` owns the mutable runtime Bambu configuration. Local Portal saves are serialized through its update API and advance an external config revision. Any in-flight background login/identity/printer-discovery operation may persist its result only when the revision still matches the snapshot it used; otherwise that late result is stale and is discarded. This prevents a background Cloud response from reverting a newer account/region/token/printer selection saved from `:8081`.

If Cloud auth becomes invalid and a password is saved, the service automatically logs in again, replaces token/user ID and reconnects when no further verification is requested. Failed renewal backs off approximately 1/5/15/30 minutes. If Cloud asks for SMS/email/TFA during renewal, unattended retry stops at `VERIFICATION_REQUIRED`; the user completes that one step locally, then automatic background recovery continues. The firmware does not bypass second factors or auto-hammer resend endpoints.

## Codex smoke / physical acceptance

Use the final exact-SHA artifact only.

1. Verify manifest source SHA and firmware SHA256; flash only application image at manifest offset. Preserve NVS.
2. Confirm boot goes directly to Stock.
3. Confirm menu has exactly five apps in the intended order and input semantics remain correct.
4. Leave representative pages/menu >60 s and confirm no automatic app switch.
5. Weather: current data readable, only 今/明, no dividers, Bad Apple 168×126, ~10 FPS, no whole-screen flicker. For full acceptance observe ~219 s loop and exit/re-enter reset.
6. Open `http://<device-ip>:8081/` and confirm one combined HA+Bambu Integrations page.
7. HA regression: existing HTTP or CA-verified HTTPS server remains readable; no secret leak.
8. Bambu: choose the correct region. For a China account enter the phone number + password directly in the local page (or email if that account uses email); for Global use email + password. Click login. If Cloud requests SMS/email/TFA, confirm the page shows the typed verification UI, enter the one-time code locally and continue. Then obtain the printer list, select the printer and confirm Cloud MQTT online.
9. For SMS/email verification, do not repeatedly resend. If resend must be tested, trigger it manually once and confirm the local 60-second cooldown. TFA should not offer a resend action.
10. After a successful verified login, reboot normally without erasing NVS and confirm a still-valid stored token resumes without asking for another verification code.
11. When practical, verify Cloud data while T-Display cannot reach printer LAN but still has Internet access.
12. During an active print verify progress, ETA, layers, nozzle/bed/chamber temperatures, job and filament/AMS fields update where available.
13. Leave Bambu for another app, wait for printer state to change, return and confirm cached state is fresh; MQTT must not be tied to active app.
14. While MQTT stays connected, verify Stock/Weather/HA remain responsive.
15. Interrupt/recover Wi-Fi and confirm Bambu reconnects without watchdog, panic, reboot or monotonic heap loss.
16. If safely testable, verify invalid/expired token triggers saved-password automatic relogin. If Cloud then requests verification, confirm it stops at `VERIFICATION_REQUIRED`; submit the code once through `:8081` and verify recovery resumes. Do not deliberately trigger account lockout. Mark NOT TESTED when unsafe/impractical.
17. If practical without risky credential experiments, save a newer Bambu config while a background Cloud operation is in flight and confirm a late result cannot restore the older config. If this timing is impractical, mark the race-specific physical case NOT TESTED; the automated revision contract remains required.
18. For formal full acceptance perform >=100 app/menu transitions and collect concise serial/system evidence.

A Cloud verification challenge is an expected recoverable state, not a bypass target. Normal China phone+password login must not be blocked locally by browser email validation or firmware email-only validation. Passwords, tokens, one-time codes and challenge secrets must remain absent from serial logs and screenshots.

## Expected diagnostics

```text
[md]      Stock
[appdata] WEATHER / HOME_ASSISTANT
[net]     short-lived HTTP/TLS transport; HA_HTTP or HA_CA where applicable
[sys]     MENU|STOCK|WEATHER|BAMBU|HOME_ASSISTANT|DEVICE_INFO
```

Bambu credentials, verification codes, challenge secrets and full authentication bodies must never appear in diagnostics.

Detailed criteria: `docs/hardware-acceptance.md`.