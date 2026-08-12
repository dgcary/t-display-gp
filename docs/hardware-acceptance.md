# T-Display-S3 Hardware Acceptance

This checklist separates behavior already covered by host tests from checks that require a physical LILYGO T-Display-S3 and unrestricted network access.

## Current acceptance state

- Host-domain and behavioral tests: **implemented and passing in the development environment**.
- Arduino/TFT integration contract compilation with local API stubs: **completed**.
- Real PlatformIO ESP32-S3 firmware build: **pending** because PlatformIO cannot be installed in the current isolated environment.
- Physical flash/display/buttons/captive-portal acceptance: **pending** until the actual board is attached.
- Full live provider matrix: **pending** for endpoints the current environment cannot reach directly.

Do not change any PENDING item to PASS without running the corresponding procedure.

## 1. Build and flash

On a machine with PlatformIO and USB access:

```bash
pio test -e native
pio run -e lilygo-t-display-s3
pio run -e lilygo-t-display-s3 -t upload
pio device monitor -b 115200
```

For first-boot provisioning acceptance, erase NVS/flash before the upload if the board has prior Wi-Fi/configuration state.

Pass criteria:

- firmware builds without warnings promoted to errors by project settings;
- device boots repeatedly without reset loop;
- Serial output does not show allocation, watchdog, queue or HTTP worker startup failures.

## 2. Display and buttons

Hardware expectations:

- display power GPIO15 is asserted HIGH before TFT initialization;
- TFT_eSPI uses LCD backlight GPIO38 with active-HIGH control;
- portrait display is 170×320;
- previous stock button is GPIO0;
- next stock button is GPIO14;
- buttons use internal pull-up and active-low input.

Pass criteria:

- screen powers on with correct orientation and no obvious corruption;
- Chinese names are legible;
- current price/change/open/high/low/prev-close/volume/amount fit the intended regions;
- intraday chart fits without crossing the footer;
- lunchtime is visibly discontinuous rather than drawing a false 11:30→13:00 line;
- each physical press advances exactly one stock; holding a button does not auto-repeat;
- repeated page changes do not cause obvious full-screen flashing beyond intentional full redraw on symbol change.

## 3. Factory provisioning

With Wi-Fi/configuration erased or invalid:

1. Power the board.
2. Confirm AP `TDisplay-GP-Setup` appears without a V1 password.
3. Connect a phone to that AP.
4. Confirm the captive portal opens; if the OS does not auto-open it, browse to `192.168.4.1`.
5. Select Wi-Fi and enter 3–5 A-share symbols, optional names, and a 3–5 second quote interval.
6. Save.

Pass criteria:

- no serial terminal is required for initial setup;
- invalid stock count/code/refresh interval is rejected rather than persisted;
- valid settings survive restart;
- after successful Wi-Fi association the normal stock UI starts.

## 4. LAN settings page

While the device is on the normal LAN:

- open the device IP in a phone browser;
- change the stock pool and/or refresh interval;
- save;
- exercise the dedicated Wi-Fi reconfiguration action.

Pass criteria:

- `/api/status` is reachable;
- `/api/config` returns/saves valid configuration;
- settings save first, then the device performs the controlled restart;
- Wi-Fi reconfiguration restarts into the captive portal;
- no half-updated running controller state is visible before restart.

## 5. Live market-data smoke test

Suggested symbols:

- SSE: `600519` 贵州茅台
- SZSE: `300750` 宁德时代
- BSE: `920047` 诺思兰德

During an active A-share session, verify for each supported symbol:

- current price and daily metrics are plausible against a second market source;
- provider timestamp is for the current China-local trading date;
- current symbol refreshes at the configured 3–5 second interval;
- intraday trend refreshes independently every 60 seconds;
- cached data stays visible if one network request fails.

BSE-specific rule: if Tencent `bj920047` does not return the expected quote schema on the actual network, record Tencent+BSE as unsupported and keep BSE on EastMoney rather than guessing a new prefix or parser.

## 6. Session-state acceptance

Host acceptance tests already cover the scheduler with a controlled clock. Real-device verification should still observe these transitions once convenient:

- pre-open: no 3–5 second high-frequency loop;
- 09:30/09:31: trading refresh starts without being blocked by yesterday's cache;
- 11:30→13:00 lunch: quote polling drops to 60 seconds and chart keeps the lunch gap;
- 13:00: normal trading refresh resumes;
- after 15:00: UI shows closed state and retains final quote/chart;
- next trading day: refresh resumes automatically;
- weekday holiday: a successful stale provider quote after the 09:31 grace point confirms NON_TRADING_DAY rather than remaining in a high-frequency loop.

## 7. Resilience acceptance

### Wi-Fi interruption

1. Let valid quote/chart data load.
2. Disable the AP/router or otherwise block Wi-Fi for at least two minutes.
3. Use both stock buttons while offline.
4. Restore Wi-Fi.

Pass criteria:

- cached screens remain usable;
- no UI freeze waiting for HTTP;
- offline/error badge is visible as applicable;
- data resumes without reboot after connectivity returns.

### Provider fallback

In a controlled build/test network, make EastMoney quote requests fail while Tencent remains reachable.

Pass criteria:

- after three EastMoney failures within 60 seconds, quote provider changes to Tencent;
- intraday cache is retained;
- EastMoney recovery probes occur no faster than once per 120 seconds;
- two successful primary probes restore EastMoney;
- a failed recovery probe resets the consecutive recovery count.

## 8. Long-run check

Leave the device powered through at least one full session transition, preferably overnight.

Pass criteria:

- no watchdog reboot or progressive UI slowdown;
- no obvious heap exhaustion after repeated HTTP/JSON operations;
- next-day transition does not treat yesterday's cached timestamp as proof that the new day is closed;
- persisted Wi-Fi and stock configuration survive ordinary power cycling.

## Acceptance record

Record real-device results here or in a GitHub issue/PR with firmware commit SHA, board revision, Wi-Fi environment, date and observed failures. The current development session intentionally leaves physical-only checks as PENDING.
