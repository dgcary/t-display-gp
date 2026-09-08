# Multi-App Shell + Weather Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the stock-only firmware into a reusable multi-app shell, preserve the validated stock experience, and add a working Open-Meteo weather app.

**Architecture:** Keep the existing stock controller/provider/worker intact behind `StockApp`. Add an `AppManager`/`MenuApp` shell, a wrap-safe short/long input layer, a low-frequency `AppDataWorker` for weather, and serialize external TLS requests inside the existing HTTP transport boundary. Extend the existing config schema compatibly so v1 stock settings migrate to v2 with optional location/weather fields.

**Tech Stack:** ESP32-S3, Arduino/C++17, PlatformIO, FreeRTOS, TFT_eSPI, U8g2_for_TFT_eSPI, ArduinoJson 6.21.6, WiFiManager, Unity native tests.

**Spec:** `docs/superpowers/specs/2026-08-19-multi-app-shell-weather-design.md`

## Global Constraints

- Hardware remains LILYGO T-Display-S3; logical display remains 320x170 landscape rotation 3.
- Existing stock Provider/QoS/retry/TTL/color/chart semantics must remain unchanged.
- Existing v1 stock configuration must migrate without losing symbols, display names, or quote refresh interval.
- GPIO0/GPIO14 short presses remain stock previous/next; GPIO0 long returns to menu; GPIO14 long enters selected menu app.
- Debounce remains 40 ms; long press threshold is 700 ms; no short-after-long or hold repeat.
- At most one external HTTP/TLS request executes at a time.
- Existing connect/read/TLS timeouts remain 1500 ms / 2500 ms / 5 s, with reuse disabled.
- Weather refresh defaults to 15 minutes and preserves cached data on failure.
- Do not require PSRAM for normal operation.
- Do not merge `main` in this implementation task.

---

### Task 1: Configuration schema v2 and migration

**Files:**
- Modify: `lib/core/AppConfig.h`
- Modify: `lib/core/AppConfig.cpp`
- Modify: `src/device/ConfigStore.cpp`
- Modify: `test/test_app_config/test_main.cpp`

**Interfaces:**
- Produces `LocationConfig`, `WeatherConfig`, schema version 2, and `AppConfigCodec::decode(..., uint32_t* sourceSchemaVersion = nullptr)`.
- Existing `config.stocks` and `config.quoteRefreshSec` remain valid API for stock code.

- [ ] **Step 1: Write failing config tests**

Add tests asserting defaults are schema 2 with weather disabled/15 min, v2 round-trip preserves location/weather, invalid coordinates/refresh fail validation, and decoding a known schema-1 payload returns schema-2 in-memory config while reporting source schema 1.

Representative assertions:

```cpp
AppConfig decoded;
uint32_t sourceSchema = 0;
TEST_ASSERT_TRUE(AppConfigCodec::decode(v1Json, decoded, &sourceSchema));
TEST_ASSERT_EQUAL_UINT32(1, sourceSchema);
TEST_ASSERT_EQUAL_UINT32(2, decoded.schemaVersion);
TEST_ASSERT_EQUAL_STRING("600519.SH", decoded.stocks[0].symbol.canonical().c_str());
TEST_ASSERT_FALSE(decoded.weather.enabled);
TEST_ASSERT_EQUAL_UINT32(15, decoded.weather.refreshMinutes);
```

- [ ] **Step 2: Run native tests and verify RED**

Run: `pio test -e native -f test_app_config`
Expected: compile/test failure because v2 location/weather fields and migration API do not exist.

- [ ] **Step 3: Implement minimal schema v2**

Use integer microdegrees to avoid locale/float JSON ambiguity:

```cpp
struct LocationConfig {
  std::string displayName;
  int32_t latitudeE6 = 0;
  int32_t longitudeE6 = 0;
};
struct WeatherConfig {
  bool enabled = false;
  uint32_t refreshMinutes = 15;
};
```

Extend codec with signed integer parsing. Encode v2 fields as `location_name`, `latitude_e6`, `longitude_e6`, `weather_enabled` (0/1), and `weather_refresh_min`. Decode both schema 1 and 2; normalize schema 1 to in-memory schema 2. Validation accepts weather refresh 5..60 minutes and validates location ranges/name when weather is enabled.

- [ ] **Step 4: Persist migration safely**

`ConfigStore::load()` asks the codec for `sourceSchemaVersion`; after successful v1 decode/validation, save the normalized v2 config back into the existing `stockticker` namespace. Failure to rewrite migration must not invalidate the already decoded usable config.

- [ ] **Step 5: Run tests GREEN**

Run: `pio test -e native -f test_app_config`
Expected: PASS.

---

### Task 2: Provisioning form extends to location/weather

**Files:**
- Modify: `src/network/ProvisioningForm.h`
- Modify: `src/network/ProvisioningForm.cpp`
- Modify: `test/test_provisioning_form/test_main.cpp`
- Modify: `tools/validate_provisioning_contract.py`

**Interfaces:**
- `ProvisioningFields` gains `locationName`, `latitude`, `longitude`, `weatherEnabled`, `weatherRefresh`.
- `ProvisioningForm::buildConfig` produces the same v2 `AppConfig` used by ConfigStore.

- [ ] **Step 1: Add failing provisioning tests**

Cover decimal coordinates, disabled-weather blank location, invalid latitude/longitude, invalid weather refresh, and `fromConfig()` round-trip.

- [ ] **Step 2: Verify RED**

Run: `pio test -e native -f test_provisioning_form`
Expected: FAIL because new fields do not exist.

- [ ] **Step 3: Implement parsing/validation**

Use `strtod` in form parsing, require full-string consumption, finite values, convert to integer microdegrees with rounding, and leave stock validation unchanged. `weatherEnabled` accepts `1`, `true`, or `on`; web code emits `1/0` deterministically.

- [ ] **Step 4: Extend provisioning contract validator**

Require the service source to expose location/weather fields while retaining all existing portal exit/reboot contract checks.

- [ ] **Step 5: Verify GREEN**

Run: `pio test -e native -f test_provisioning_form`
Expected: PASS.

---

### Task 3: Short/long input and app-shell core

**Files:**
- Create: `src/device/ButtonInput.h`
- Create: `src/app/AppShell.h`
- Create: `src/app/AppShell.cpp`
- Create: `test/test_button_input/test_main.cpp`
- Create: `test/test_app_shell/test_main.cpp`
- Modify: `platformio.ini`
- Modify: `include/build_config.h`

**Interfaces:**

```cpp
enum class InputEvent { NONE, PREV_SHORT, NEXT_SHORT, PREV_LONG, NEXT_LONG };
enum class AppId { MENU, STOCK, WEATHER };
```

`ButtonInput` exposes a pure/native-testable update API for one active-low button and differentiates short vs long. `AppManager` routes global navigation and only ticks/renders the active app.

- [ ] **Step 1: Add failing ButtonInput tests**

Cover 40-ms debounce, release-generated short press, 700-ms long press, single-fire long, no short after long, and rollover-safe timing.

- [ ] **Step 2: Verify ButtonInput RED**

Run: `pio test -e native -f test_button_input`
Expected: FAIL because ButtonInput does not exist.

- [ ] **Step 3: Implement ButtonInput minimally**

Use unsigned elapsed arithmetic `static_cast<uint32_t>(now - then)`, emit long once while held, and emit short only on debounced release when no long was emitted.

- [ ] **Step 4: Add failing AppManager tests**

Use fake apps that count enter/exit/button/tick/render calls. Verify startup stock, Stock->Menu on PREV_LONG, menu selection wrapping, NEXT_LONG entering selected app, inactive app isolation, and reserved long-right in normal apps.

- [ ] **Step 5: Implement `IApp`, `MenuApp`, and `AppManager` core**

Keep UI rendering behind app objects. `MenuApp` receives app descriptors and stores selection; `AppManager` owns transitions/global semantics.

- [ ] **Step 6: Run both suites GREEN**

Run: `pio test -e native -f test_button_input -f test_app_shell`
Expected: PASS.

---

### Task 4: Weather provider/parser and controller

**Files:**
- Create: `src/network/WeatherProvider.h`
- Create: `src/network/OpenMeteoProvider.cpp`
- Create: `src/app/WeatherController.h`
- Create: `src/app/WeatherController.cpp`
- Create: `test/test_weather_provider/test_main.cpp`
- Create: `test/fixtures/open_meteo_forecast.json`
- Create: `test/test_weather_controller/test_main.cpp`
- Modify: `platformio.ini`

**Interfaces:**

`WeatherSnapshot` is bounded structured state. `IWeatherProvider::fetch(const LocationConfig&, WeatherSnapshot&, ProviderDiagnostics*)` uses `IHttpTransport`. `IAppDataQueue` exposes enqueue/tryReceive for weather. `WeatherController` maintains cache/health and refresh scheduling.

- [ ] **Step 1: Add provider RED tests**

Fixture includes current temperature, apparent temperature, humidity, wind, weather code, and three daily forecast entries. Verify URL contains configured latitude/longitude/timezone, valid payload parsing, malformed/missing arrays fail closed, HTTP errors propagate, and existing output object is not mutated on parse failure.

- [ ] **Step 2: Implement Open-Meteo provider GREEN**

Build the forecast URL with three days and `timezone=Asia%2FShanghai`. Parse into a temporary `WeatherSnapshot` with ArduinoJson and move to output only after complete validation.

- [ ] **Step 3: Add WeatherController RED tests**

Fake queue tests cover no-cache initial request, 15-minute scheduling, successful result cache, failure preserving cache, offline no enqueue, and inactive controller no scheduling/redraw state.

- [ ] **Step 4: Implement WeatherController GREEN**

Track configured/enabled/active/wifi/outstanding state plus last attempt/success/error. Expose a view model and dirty/full-redraw flags similar to the stock controller pattern.

- [ ] **Step 5: Run weather native tests**

Run: `pio test -e native -f test_weather_provider -f test_weather_controller`
Expected: PASS.

---

### Task 5: Firmware workers, network serialization, menu/weather screens, app wrappers

**Files:**
- Create: `src/network/AppDataWorker.h`
- Create: `src/network/AppDataWorker.cpp`
- Create: `src/ui/MenuScreen.h`
- Create: `src/ui/MenuScreen.cpp`
- Create: `src/ui/WeatherScreen.h`
- Create: `src/ui/WeatherScreen.cpp`
- Create: `src/app/StockApp.h`
- Create: `src/app/StockApp.cpp`
- Create: `src/app/WeatherApp.h`
- Create: `src/app/WeatherApp.cpp`
- Modify: `src/network/HttpTransport.cpp`
- Modify: `src/device/DeviceLayer.h`
- Modify: `src/device/DeviceLayer.cpp`
- Modify: `src/main.cpp`
- Modify: `platformio.ini`

**Interfaces:**
- `AppDataWorker` implements `IAppDataQueue`, has one bounded FreeRTOS worker/queue, and only WEATHER request type in V1.
- `HttpTransport::get` obtains a shared firmware mutex/arbiter before creating/using TLS and releases it on every return path.
- `StockApp` maps short input events to the existing `StockController::onButton(PREVIOUS/NEXT)` and otherwise leaves stock logic unchanged.
- `WeatherApp` owns controller/screen and uses DeviceLayer for Wi-Fi state.

- [ ] **Step 1: Implement network serialization with contract guard**

Add a RAII-style lock around each external `HttpTransport::get` operation; do not change timeout/reuse/TLS settings. Extend `tools/validate_http_transport_contract.py` to require the arbiter/lock marker.

- [ ] **Step 2: Implement AppDataWorker**

Use one FreeRTOS task, bounded request/result queues, no inline UI blocking, and concise `[appdata]` diagnostics. Do not add app-specific worker tasks.

- [ ] **Step 3: Implement MenuScreen/WeatherScreen**

Keep 320x170 rotation/layout. Use text/simple symbols only, no large bitmap assets. Weather condition codes map to concise Chinese labels. Cached/stale/error state stays readable.

- [ ] **Step 4: Implement StockApp/WeatherApp wrappers**

StockApp initializes the existing MarketDataWorker/controller/screen once. Suspended apps are not ticked/rendered by AppManager, so stock scheduling pauses naturally while cache objects stay resident. WeatherApp follows the same lifecycle.

- [ ] **Step 5: Replace DeviceLayer edge input with InputEvent generation**

Maintain two independent ButtonInput instances and return at most one logical event per loop. Preserve pull-up active-low GPIO0/GPIO14 behavior.

- [ ] **Step 6: Rewire main.cpp**

Provisioning/time setup remains common. Initialize StockApp and WeatherApp, register them with MenuApp/AppManager, default to STOCK, then loop through provisioning, input, `AppManager::tick`, and `AppManager::render`.

- [ ] **Step 7: Compile firmware**

Run: `pio run -e lilygo-t-display-s3`
Expected: SUCCESS.

---

### Task 6: Web/captive configuration and documentation

**Files:**
- Modify: `src/network/ProvisioningService.cpp`
- Modify: `README.md`
- Modify: `AGENTS.md`
- Modify: `docs/deployment.md`
- Modify: `docs/hardware-acceptance.md`
- Modify: `tools/validate_tdisplay_setup.py` if source wiring/contract requires it

- [ ] **Step 1: Extend Web settings UI**

Add a Weather/Location card with location name, latitude, longitude, enabled checkbox, and refresh minutes. `/api/status` continues returning encoded config. `/api/config` parses all stock+weather fields atomically and retains reboot-on-save behavior.

- [ ] **Step 2: Extend WiFiManager captive portal fields**

Add bounded buffers/parameters for the same weather/location settings so a first-boot device can create a complete v2 config. Do not weaken the deterministic portal exit/reboot behavior.

- [ ] **Step 3: Update docs**

Document menu controls, startup behavior, weather setup, Open-Meteo role, schema migration, network serialization, and physical acceptance sequence.

- [ ] **Step 4: Run contract validators**

Run:

```text
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
```

Expected: all PASS.

---

### Task 7: Full regression, resource verification, and PR handoff

**Files:**
- No production changes unless verification exposes a defect.

- [ ] **Step 1: Run full native tests on Ubuntu-compatible native environment**

Run: `pio test -e native`
Expected: every existing and new suite PASS.

- [ ] **Step 2: Verify Windows native through CI**

Push branch and require `native-windows` PASS; do not exclude new portable core/provider/controller sources merely to make Windows green.

- [ ] **Step 3: Build ESP32-S3 firmware**

Run: `pio run -e lilygo-t-display-s3`
Expected: SUCCESS; record RAM and Flash usage.

- [ ] **Step 4: Review diff for forbidden scope changes**

Confirm no stock provider/QoS/retry/color/chart/pin/dependency-version changes beyond necessary wrapper/wiring additions.

- [ ] **Step 5: Open a Draft PR**

Base `main`, head `feature/multi-app-shell-weather`. Report exact head SHA, tests, resources, and mark COM6 physical acceptance pending. Do not merge main.
