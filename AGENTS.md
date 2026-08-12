# AGENTS.md — T-Display GP

This repository is the source of truth for the T-Display GP firmware.
These instructions apply to Codex and other automated coding/deployment agents.

## Target

- Repository: `dgcary/t-display-gp`
- Hardware: **LILYGO T-Display-S3 only**
- MCU: ESP32-S3
- Display: ST7789 170x320 portrait
- Framework: Arduino/C++ via PlatformIO
- Primary PlatformIO environment: `lilygo-t-display-s3`

Do not silently retarget the firmware to another T-Display revision, ESP32 board, display controller, or pinout.

## Source-of-truth rule

Before development or deployment, start from the requested GitHub branch/commit.
Do not treat an old local checkout, generated binary, or previous conversation artifact as authoritative.

For ordinary production-style deployment, prefer the latest approved `main` commit. If the user explicitly asks to test a feature branch or commit SHA, deploy exactly that ref and record it.

## Required pre-deployment sequence

Run in this order:

```bash
pio test -e native
pio run -e lilygo-t-display-s3
pio run -e lilygo-t-display-s3 -t upload
pio device monitor -b 115200
```

Rules:

1. Do not upload if native tests fail.
2. Do not upload if the ESP32-S3 firmware build fails.
3. Do not convert warnings/errors into ignored status merely to make the build green.
4. Do not claim hardware PASS without a physical board check.
5. Record the exact Git commit SHA used for every hardware acceptance run.

## First physical acceptance

After flashing a new or erased board, verify at minimum:

- LCD powers on and orientation is correct.
- A-share positive change is visually red, not blue because of RGB/BGR mismatch.
- Chinese stock names render legibly.
- GPIO0 moves to previous stock exactly once per press.
- GPIO14 moves to next stock exactly once per press.
- Holding a key does not auto-repeat.
- `TDisplay-GP-Setup` appears when configuration is absent/forced.
- A phone can complete Wi-Fi + 3–5 stock provisioning.
- Device reaches live market data without blocking the UI.

For complete acceptance use `docs/hardware-acceptance.md`.

## Hardware invariants

Do not change these unless the user explicitly changes the target hardware and the design is updated:

- GPIO15: display power, HIGH before TFT initialization
- GPIO38: TFT backlight
- GPIO0: previous stock button, pull-up, active low
- GPIO14: next stock button, pull-up, active low
- TFT: ST7789, 170x320, 8-bit parallel
- TFT color order: `TFT_RGB`
- TFT init: `INIT_SEQUENCE_3`

`platformio.ini` is expected to stay aligned with TFT_eSPI's official `Setup206_LilyGo_T_Display_S3.h`.
Run `tools/validate_tdisplay_setup.py` when modifying TFT build flags.

## Architecture invariants

- `main loop` must not perform blocking market HTTP calls.
- HTTP work belongs in `MarketDataWorker`.
- UI/controller must depend on Provider abstractions, not raw provider response formats.
- EastMoney remains primary for V1 quote + intraday.
- Tencent remains V1 quote fallback.
- Network/provider failure must preserve the last valid cached screen.
- Configuration changes are persisted then applied by controlled restart; do not partially hot-mutate runtime state in V1.

## Market-data caution

The public EastMoney/Tencent endpoints are unofficial/unsupported contracts and may change.
When a provider fails:

1. Capture the actual response/status.
2. Add or update a regression fixture/test first.
3. Change only the provider/parser boundary where possible.
4. Do not weaken parser validation just to accept unknown malformed data.
5. Preserve the provider abstraction so another source can replace it.

See `docs/api-contract.md`.

## Test discipline

For behavior changes, use test-first development where practical.
At minimum, run the directly affected test and then the complete native suite before deployment.

Do not mark a physical acceptance item PASS from:

- a host mock/stub compile;
- a parser fixture;
- a successful firmware compile without upload;
- screenshots or assumptions from a different T-Display model.

## Scope and safety

Deployment work is limited to the connected T-Display-S3 and this repository.
Do not modify unrelated repositories, servers, routers, DHCP, Wi-Fi infrastructure, or other network devices as part of flashing this board.

Do not hard-code Wi-Fi passwords, GitHub credentials, API secrets, broker credentials, or personal account information into the repository.

## Documentation

If behavior, pins, Provider contracts, build commands, or first-boot UX change, update the relevant documentation in the same commit/PR:

- `README.md`
- `docs/deployment.md`
- `docs/api-contract.md`
- `docs/hardware-acceptance.md`

A code change that makes these documents materially wrong is incomplete.
