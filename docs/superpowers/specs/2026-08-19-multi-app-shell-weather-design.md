# T-Display GP Multi-App Shell + Weather Design

Date: 2026-08-19
Status: Approved
Base branch: `main`
Base commit: `6dcdc0e694d7d01c5ca77a8a0b7a23469d883925`
Feature branch: `feature/multi-app-shell-weather`

## 1. Goal

Evolve T-Display GP from a single-purpose stock ticker into a small multi-application desktop terminal without destabilizing the existing stock feature.

The first implementation milestone contains only:

- a reusable multi-app shell,
- a main menu,
- the existing stock feature wrapped as `StockApp`,
- a new `WeatherApp` used to validate the architecture end to end.

Future applications such as air quality, Home Assistant, and service monitoring are intentionally not implemented in this milestone, but the architecture must make them straightforward to add later.

## 2. Non-goals

This milestone does not implement:

- air-quality UI/provider logic,
- Home Assistant integration,
- service monitoring,
- dynamic plugin loading,
- a general operating system/service bus,
- always-on background refresh for every app,
- a rewrite of the stock scheduler,
- changes to stock providers, quote QoS, retry policy, chart layout, A-share colors, TFT pinout, or hardware target.

Hardware remains LILYGO T-Display-S3 with a logical 320x170 landscape display.

## 3. Architectural approach

Use a lightweight `AppManager` plus independent application modules.

```text
T-Display GP
    |
    +-- AppManager
    |    +-- MenuApp
    |    +-- StockApp
    |    |    +-- StockController
    |    |    +-- StockScreen
    |    |    +-- MarketDataWorker
    |    +-- WeatherApp
    |         +-- WeatherController
    |         +-- WeatherScreen
    |         +-- WeatherService
    |
    +-- Shared Services
         +-- DeviceLayer
         +-- input handling
         +-- Wi-Fi
         +-- Time/NTP
         +-- ConfigStore
         +-- LocationConfig
         +-- shared HTTP/TLS arbitration
```

The existing stock controller, screen, provider, and worker logic should be preserved as much as possible. The objective is to wrap the existing stock feature, not rewrite it.

## 4. App contract and lifecycle

Each app has one clear lifecycle and input/render boundary. A minimal interface exposes behavior equivalent to:

```cpp
class IApp {
 public:
  virtual ~IApp() = default;
  virtual AppId id() const = 0;
  virtual const char* name() const = 0;
  virtual void onEnter() = 0;
  virtual void onExit() = 0;
  virtual void onButton(ButtonEvent event) = 0;
  virtual void tick(uint32_t nowMs) = 0;
  virtual void render(bool fullRedraw) = 0;
};
```

Exact naming may vary during implementation, but the separation of responsibilities must remain.

Lifecycle semantics:

- `onEnter()` activates app-specific scheduling and marks a full redraw.
- `onExit()` stops new foreground scheduling for that app.
- Exiting does not destroy the app object or clear its valid cache.
- Suspended apps do not draw to the TFT.
- Suspended apps do not receive normal app button events.
- Re-entering shows last valid cached state immediately, then resumes refresh logic.

## 5. AppManager responsibilities

`AppManager` owns app switching and global navigation semantics. It must not contain stock-, weather-, or provider-specific business logic.

Responsibilities:

- track the active app,
- switch between menu and registered apps,
- route button events,
- call `onEnter()`/`onExit()` exactly once per transition,
- ensure only the active app renders,
- expose the registered app list to `MenuApp`,
- default to `StockApp` after boot.

The app list is registration-driven so future apps can be added without redesigning menu navigation.

## 6. Input model

The existing edge-only buttons are expanded into four logical events:

```text
PREV_SHORT
NEXT_SHORT
PREV_LONG
NEXT_LONG
```

Timing and semantics:

- debounce remains 40 ms,
- long-press threshold is 700 ms,
- a long press fires once only,
- a long press does not also emit the corresponding short event on release,
- hold does not auto-repeat,
- timing is wrap-safe across `millis()` rollover.

Global navigation:

Inside a normal app:

- GPIO0 short -> app-specific previous action,
- GPIO14 short -> app-specific next action,
- GPIO0 long -> return to main menu unconditionally,
- GPIO14 long -> reserved/no action in V1.

Inside the menu:

- GPIO0 short -> previous menu item,
- GPIO14 short -> next menu item,
- GPIO0 long -> no action,
- GPIO14 long -> enter selected app.

Existing stock short-press behavior remains unchanged from the user's perspective.

## 7. Main menu UI

Use a horizontal app carousel rather than a dense grid because the display is only 320x170.

Concept:

```text
+--------------------------------------+
| T-Display GP          11:46   WiFi * |
|                                      |
|    +--------+    +--------+          |
|    | Stock  |    | Weather|          |
|    |   [ ]  |    |   [ ]  |          |
|    +--------+    +--------+          |
|       ^ selected                     |
|                                      |
| < previous   hold right   next >     |
+--------------------------------------+
```

Exact iconography may use text/simple symbols depending on current font support. Legibility and deterministic rendering matter more than decorative graphics.

The menu consumes the app registry and must not hard-code an assumption that only two apps will ever exist.

## 8. Startup behavior

V1 remains stock-first to preserve current device behavior:

```text
Boot -> provisioning/Wi-Fi -> common services -> AppManager -> StockApp
```

A future config may support stock/weather/menu/last-app startup targets, but that is out of scope now.

## 9. StockApp migration

`StockApp` wraps the already validated stock feature and retains:

- `StockController`,
- `StockScreen`,
- `MarketDataWorker`,
- EastMoney primary quote + intraday,
- Tencent quote fallback,
- quote-first QoS,
- intraday retry semantics,
- request TTLs,
- diagnostics,
- cache behavior,
- active-trading delay logic,
- current 320x170 stock UI.

When suspended:

- stop scheduling new quote/intraday refreshes,
- keep last valid quote/intraday caches,
- allow an already executing HTTP request to finish naturally,
- do not let completion of a background stock request redraw the TFT.

When re-entered:

- render existing stock cache immediately,
- resume normal stock refresh scheduling.

## 10. WeatherApp scope

Weather is the second real app and the architecture validation target.

Display fields:

- configured location name,
- current temperature,
- weather condition,
- apparent temperature,
- humidity,
- wind speed,
- precipitation probability when available,
- high/low for today,
- high/low for tomorrow,
- high/low for the following day,
- last successful update time,
- concise stale/error indicator when appropriate.

Default weather refresh interval: 15 minutes.

On entry:

1. render cached data immediately when available,
2. inspect cache age,
3. enqueue a background weather request if refresh is due,
4. update the screen only if WeatherApp is still active.

On request failure:

- retain the last valid snapshot,
- do not clear the screen,
- expose a weather-specific stale/error indicator,
- do not propagate a global device network error.

## 11. Weather provider abstraction

Weather UI/controller do not depend directly on an API payload format.

```text
WeatherApp -> WeatherService -> IWeatherProvider -> OpenMeteoProvider
```

First provider: Open-Meteo.

Provider replacement in the future must not require Weather UI/controller rewrites. A QWeather provider can be evaluated separately if regional reliability or feature requirements justify it.

## 12. Weather data model

Do not retain raw response JSON after parsing. Keep a bounded structured snapshot, conceptually:

```cpp
struct DailyForecast {
  float highTemp;
  float lowTemp;
  int weatherCode;
};

struct WeatherSnapshot {
  float currentTemp;
  float apparentTemp;
  int humidityPercent;
  float windSpeed;
  int precipitationProbabilityPercent;
  int weatherCode;
  DailyForecast today;
  DailyForecast tomorrow;
  DailyForecast dayAfter;
  uint64_t updatedEpochSeconds;
};
```

Parsing fails closed on malformed/materially incomplete payloads. Parser failure never overwrites a valid cached snapshot.

## 13. Location and weather configuration

No automatic IP geolocation in V1.

Web configuration gains shared location fields:

```text
Location name
Latitude
Longitude
```

Weather configuration gains:

```text
Enabled
Refresh interval in minutes
```

Conceptually:

```cpp
struct LocationConfig {
  std::string displayName;
  double latitude;
  double longitude;
};

struct WeatherConfig {
  bool enabled;
  uint32_t refreshMinutes;
};
```

`LocationConfig` is shared so a future AirQualityApp can reuse it.

## 14. Configuration schema migration

The existing stock-centric configuration must migrate without losing the user's stock list.

Target device-level model is conceptually:

```text
DeviceConfig
  +-- GeneralConfig
  |     +-- startupApp
  +-- LocationConfig
  +-- StockConfig
  |     +-- stocks
  |     +-- quoteRefresh
  +-- WeatherConfig
  +-- future HomeAssistantConfig
  +-- future ServiceMonitorConfig
```

Requirements:

- current v1 stock configuration decodes successfully,
- migration preserves stock symbols, display names, and quote refresh setting,
- new location/weather fields receive safe defaults,
- migration is covered by native tests,
- keep the existing NVS namespace in this milestone.

## 15. Network execution model

Do not create one FreeRTOS worker per app.

Keep the specialized `MarketDataWorker` because its market-specific QoS/retry behavior is already tested.

Add one shared low-frequency app-data path:

```text
MarketDataWorker      AppDataWorker
      \                 /
       \               /
        NetworkArbiter
              |
         HttpTransport
```

Only WEATHER is implemented as an AppDataWorker request type now. Future request types may include AIR_QUALITY, HOME_ASSISTANT, and SERVICE_STATUS.

### 15.1 NetworkArbiter

At most one external HTTP/TLS request executes at a time.

The arbiter exists to avoid concurrent TLS handshakes/body buffers that can create runtime heap spikes. It must not change existing HTTP timeout semantics:

- connect timeout 1500 ms,
- HTTP/read timeout setting 2500 ms,
- TLS handshake cap 5 s,
- connection reuse disabled unless separately approved.

Do not weaken TLS behavior as part of this feature.

## 16. Memory strategy

The architecture must not require PSRAM simply to remain stable.

Rules:

- core functionality must fit internal RAM,
- PSRAM may be used later for optional larger caches/assets,
- each app stores only bounded structured state,
- raw HTTP bodies are discarded after parsing,
- no unbounded queues,
- do not create a task per app,
- no image-heavy menu assets in V1.

Runtime acceptance records:

- free heap,
- minimum free heap,
- PSRAM availability/usage if enabled,
- relevant task stack high-water marks.

## 17. In-flight request semantics during app switching

Existing synchronous HTTP execution is not force-cancelled when an app exits.

`onExit()` stops new scheduling. A request already executing may finish naturally within existing bounded network timeouts. Its result may update that app's cache, but a suspended app must not redraw the display.

Do not use `vTaskDelete()` to abort a TLS operation.

## 18. Error isolation

Each app maintains its own health state, such as:

```text
lastAttempt
lastSuccess
lastError
cacheAge
```

Weather/provider failures affect WeatherApp only. Future Home Assistant/service failures affect only their apps. Do not introduce a global `networkError` that turns unrelated apps into error states.

## 19. Testing requirements

### Input

- 40 ms debounce preserved,
- short press emitted correctly,
- long press at 700 ms,
- long press emitted once,
- long release does not emit short,
- no auto-repeat,
- wrap-safe timing.

### AppManager

- default active app is StockApp,
- Stock -> Menu via PREV_LONG,
- menu previous/next selection wraps correctly,
- Menu -> Stock via NEXT_LONG,
- Menu -> Weather via NEXT_LONG,
- `onEnter()`/`onExit()` exactly once per transition,
- inactive apps do not receive button events,
- inactive apps do not render.

### Stock regression

- all existing native tests continue to pass,
- short GPIO0/GPIO14 behavior remains previous/next stock,
- provider/QoS/retry behavior unchanged,
- stock cache survives suspension/re-entry.

### Weather

- no-cache first entry,
- cached entry renders immediately,
- 15-minute default refresh,
- HTTP/provider failure preserves valid cache,
- malformed/missing payload fields fail closed,
- oversized/truncated body handling stays bounded,
- offline state does not clear cache,
- inactive WeatherApp result cannot redraw TFT.

### Configuration

- v1 -> v2 migration,
- stock list/display names/refresh preserved,
- location round-trip,
- weather config round-trip,
- invalid latitude/longitude/refresh rejected.

### Builds

Required before merge:

```text
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
```

Existing Ubuntu + Windows native CI must stay green.

## 20. Hardware acceptance

Physical COM6 test after automated verification:

```text
boot -> stock
hold GPIO0 -> menu
short GPIO0/GPIO14 -> move selection
select Weather -> hold GPIO14 -> weather
hold GPIO0 -> menu
select Stock -> hold GPIO14 -> stock cache appears immediately
```

Minimum acceptance:

- 100 Stock/Menu/Weather transitions,
- no watchdog,
- no unexpected reboot,
- no freeze,
- no cache loss,
- no button double-trigger/short-after-long,
- Wi-Fi does not repeatedly reconnect due to switching,
- heap does not exhibit monotonic leakage.

## 21. Documentation and scope invariants

Update README, deployment/configuration docs, hardware acceptance docs, and `AGENTS.md` where behavior or architecture changes.

Do not merge to `main` until automated checks and the required physical acceptance for the new menu/input/weather flow are complete, unless the user explicitly overrides that gate.
