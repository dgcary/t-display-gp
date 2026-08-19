# AGENTS.md — T-Display GP

This repository is the source of truth for the T-Display GP firmware.
These instructions apply to Codex and other automated coding/deployment agents.

## Target

- Repository: `dgcary/t-display-gp`
- Hardware: **LILYGO T-Display-S3 only**
- MCU: ESP32-S3
- Physical TFT: ST7789 170×320, 8-bit parallel
- Application orientation: **320×170 landscape**
- Framework: Arduino/C++17 via PlatformIO
- Primary environment: `lilygo-t-display-s3`

Do not silently retarget to another board, display revision, controller, pinout, or orientation.

## Source-of-truth rule

Before development or deployment, fetch the requested GitHub branch/commit. Do not treat an old local checkout, binary, or previous chat artifact as authoritative.

For normal deployment use the latest approved `main`. If the user requests a feature branch or exact SHA, deploy exactly that ref and record it.

## Required pre-deployment sequence

```bash
pio test -e native
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
pio run -e lilygo-t-display-s3
pio run -e lilygo-t-display-s3 -t upload
pio device monitor -b 115200
```

Rules:

1. Do not upload if native tests or firmware build fail.
2. Do not ignore warnings/errors merely to make CI green.
3. Do not claim hardware PASS without physical-board evidence.
4. Record the exact Git SHA for every hardware acceptance run.

## Hardware invariants

- GPIO15: display power, HIGH before TFT initialization
- GPIO38: backlight
- GPIO0: previous stock, pull-up, active low
- GPIO14: next stock, pull-up, active low
- TFT color order: `TFT_RGB`
- TFT init: `INIT_SEQUENCE_3`
- Application rotation: 320×170 landscape

`platformio.ini` must remain aligned with TFT_eSPI `Setup206_LilyGo_T_Display_S3.h`. Run `tools/validate_tdisplay_setup.py` for TFT/build changes.

## Market-data architecture invariants

- Main loop must never perform blocking market HTTP.
- HTTP work belongs in `MarketDataWorker`.
- UI/Controller depend on Provider abstractions, not raw payload formats.
- EastMoney remains V1 quote + intraday primary.
- Tencent remains V1 **quote-only** fallback.
- Network/provider failure preserves the last valid quote and intraday cache.
- Quote health and intraday health are independent.
- Quote traffic has priority over intraday traffic.
- Waiting intraday work is latest-wins; do not allow old trend requests to build an unbounded queue.
- Intraday transient retry is bounded and deferred: maximum 3 total attempts; retry must yield to quote traffic.
- Market TCP connect timeout is 1500 ms, HTTP/read timeout setting is 2500 ms, and TLS handshake timeout is explicitly capped at 5 seconds.
- Do not pass millisecond timeout constants directly to `WiFiClientSecure::setTimeout()` on Arduino-ESP32 2.0.14; that API is seconds-based.
- Do not weaken parser validation.
- Do not add infinite retry, unbounded timeouts, or TLS-security weakening as a stability workaround.
- Keep `HTTPClient::setReuse(false)` unless a separately approved measurement-driven change says otherwise.

Run `tools/validate_http_transport_contract.py` whenever transport timeout/reuse logic changes.

## Request diagnostics

Market requests emit concise `[md]` serial completion lines. When diagnosing live failures, preserve at least:

- request type and stock
- provider
- attempt
- queue wait and request duration
- HTTP status
- native HTTPClient error
- TLS error when available
- received/expected bytes
- final provider result

Do not log full market response bodies by default.

## UI invariants

- Logical layout is 320×170 landscape.
- Left side contains price and compact metrics; right side contains the intraday chart.
- A-share positive change is red and negative change is green.
- Intraday chart preserves the lunch discontinuity.
- Chart includes distinct previous-close (`昨收`) and valid today-open (`今开`) reference lines.
- A single intraday failure with a fresh cached chart must not become a generic page-wide error.
- Stale quote and stale intraday are reported independently (`报价延迟` / `分时延迟`).
- Delay badges are active-trading health signals; lunch/closed/non-trading cached data aging must not produce false delay alarms.

## Provider caution

EastMoney/Tencent endpoints are public, unofficial contracts and can change. When a provider fails:

1. Capture actual device diagnostics/status first.
2. Add/update a regression fixture or behavior test before changing Provider/Parser behavior.
3. Change the Provider/transport boundary where possible.
4. Never relax parsing simply to accept unknown malformed data.
5. Do not add an intraday fallback Provider without a separate approved design review.

See `docs/api-contract.md`.

## First physical acceptance

At minimum verify:

- LCD is landscape and fully visible.
- Chinese stock names are legible.
- red/green color direction is correct.
- GPIO0/GPIO14 each trigger once per press; holds do not auto-repeat.
- `TDisplay-GP-Setup` provisioning still works.
- device reaches `[boot] market loop ready`.
- quotes continue to refresh when an intraday request fails/retries.
- cached quote/chart remain visible during failures.
- `昨收` and `今开` chart references render correctly.
- no watchdog reset or unexpected reboot.

Use `docs/hardware-acceptance.md` for complete acceptance.

## Scope and safety

Deployment is limited to the connected T-Display-S3 and this repository. Do not modify unrelated servers, routers, DHCP, Wi-Fi infrastructure, or other devices. Never hard-code Wi-Fi passwords, GitHub credentials, API secrets, broker credentials, or personal account data.

## Documentation

When behavior, pins, Provider contracts, build commands, market scheduling, or UI orientation change, update the relevant files in the same PR:

- `README.md`
- `docs/deployment.md`
- `docs/api-contract.md`
- `docs/hardware-acceptance.md`

A change that makes these documents materially wrong is incomplete.
