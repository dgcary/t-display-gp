# T-Display-S3 A股实时行情终端 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 LILYGO T-Display-S3 上交付一个可通过手机配网和配置 3–5 只 A 股、交易时段 3–5 秒刷新、实体按键切换并显示当日分时图的稳定桌面行情终端。

**Architecture:** 使用 PlatformIO + Arduino/C++，设备主循环只负责按键、Web 服务、调度和绘制；所有 HTTP 行情请求放入独立 FreeRTOS worker，避免网络超时阻塞 UI。行情源通过 `IQuoteProvider` 隔离，东方财富为主、腾讯为快照备用；配置通过 WiFiManager + NVS/Preferences 持久化，UI 使用 TFT_eSPI，并通过 U8g2_for_TFT_eSPI 支持 UTF-8 中文股票名。

**Tech Stack:** ESP32-S3 / PlatformIO `espressif32@6.5.0` / Arduino / TFT_eSPI 2.5.43 / U8g2_for_TFT_eSPI 1.7.0 / WiFiManager 2.0.17 / ArduinoJson 6.21.6 / built-in HTTPClient, WiFiClientSecure, WebServer, Preferences, time/NTP, FreeRTOS queues / PlatformIO Unity native tests.

## Global Constraints

- Target repository: `dgcary/t-display-gp`; default branch `main`.
- Hardware: LILYGO T-Display-S3, ESP32-S3R8, 16MB Flash, 8MB PSRAM, ST7789 170×320.
- Display power GPIO15 must be driven HIGH before screen initialization.
- Buttons: GPIO0 = previous stock, GPIO14 = next stock; both active-low with debounce.
- Market: A 股; user configures 3–5 symbols.
- Quote refresh: configurable 3–5 seconds; default 5 seconds during continuous trading.
- Intraday refresh: 60 seconds by default; quote and intraday schedules are independent.
- Primary quote/intraday provider: EastMoney public endpoints; fallback snapshot provider: Tencent public endpoint.
- Provider implementation must be replaceable and must not leak provider-specific fields into UI/controller code.
- Network calls must never run on the UI/main loop; they execute on a FreeRTOS worker task.
- First boot must expose a WiFiManager captive portal; normal LAN operation must expose a local settings page.
- V1 does not include K-line, auto-rotation, touch, cloud sync, push alerts, or OTA management UI.
- Public market-data endpoints are unofficial and can change; parsing must validate required fields and fail closed to cached data.
- HTTPS public-data calls may use `WiFiClientSecure::setInsecure()` in V1 because no credentials or private payloads are transmitted; document this explicitly as transport-identity trade-off.
- Do not copy the upstream monolithic `.ino` wholesale. Reuse MIT-licensed ideas/code only where useful, preserve attribution, and keep this project modular and testable.

---

## Planned File Structure

```text
.
├── .github/workflows/ci.yml                  # native tests + firmware compile
├── platformio.ini                            # pinned device/native environments
├── README.md                                 # build/flash/config/usage
├── THIRD_PARTY_NOTICES.md                    # upstream attribution and licenses
├── include/
│   └── build_config.h                        # fixed pins/timing/constants only
├── lib/
│   ├── core/
│   │   ├── QuoteModels.h                     # domain value types
│   │   ├── StockSymbol.h/.cpp                # normalization + provider symbol mapping
│   │   ├── MarketClock.h/.cpp                # China A-share session state machine
│   │   ├── Formatters.h/.cpp                 # value/volume/amount formatting
│   │   ├── AppConfig.h/.cpp                  # config schema + validation
│   │   └── ProviderFailover.h/.cpp           # provider health/failover policy
│   └── providers/
│       ├── IQuoteProvider.h                  # provider contract
│       ├── EastMoneyParser.h/.cpp            # pure response parsing
│       └── TencentParser.h/.cpp              # pure response parsing
├── src/
│   ├── main.cpp                              # wiring only
│   ├── device/
│   │   ├── DeviceLayer.h/.cpp                # TFT power/init, buttons, Wi-Fi/NTP status
│   │   └── ConfigStore.h/.cpp                # Preferences adapter
│   ├── network/
│   │   ├── HttpTransport.h/.cpp              # bounded HTTP(S) transport
│   │   ├── EastMoneyProvider.h/.cpp          # live primary provider
│   │   ├── TencentProvider.h/.cpp            # live snapshot fallback
│   │   ├── MarketDataWorker.h/.cpp           # FreeRTOS queue/task
│   │   └── ProvisioningService.h/.cpp        # WiFiManager + LAN WebServer
│   ├── app/
│   │   └── StockController.h/.cpp            # scheduler/cache/index/state
│   └── ui/
│       ├── StockScreen.h/.cpp                # 170×320 renderer
│       └── UiTheme.h                         # layout/color constants
└── test/
    ├── test_symbol/test_main.cpp
    ├── test_market_clock/test_main.cpp
    ├── test_formatters/test_main.cpp
    ├── test_app_config/test_main.cpp
    ├── test_eastmoney_parser/test_main.cpp
    ├── test_tencent_parser/test_main.cpp
    └── test_failover/test_main.cpp
```

### Task 1: Bootstrap the repository, toolchain, CI, and upstream attribution

**Files:**
- Create: `platformio.ini`
- Create: `.github/workflows/ci.yml`
- Create: `include/build_config.h`
- Create: `src/main.cpp`
- Create: `README.md`
- Create: `THIRD_PARTY_NOTICES.md`

**Interfaces:**
- Produces device environment `lilygo-t-display-s3` and host environment `native`.
- Defines `BuildConfig::PIN_POWER=15`, `PIN_BUTTON_PREV=0`, `PIN_BUTTON_NEXT=14`, `DEFAULT_QUOTE_REFRESH_MS=5000`, `INTRADAY_REFRESH_MS=60000`.

- [ ] **Step 1: Add the pinned PlatformIO environments**

```ini
[platformio]
default_envs = lilygo-t-display-s3

[env:lilygo-t-display-s3]
platform = espressif32@6.5.0
board = lilygo-t-display-s3
framework = arduino
monitor_speed = 115200
build_flags =
  -DUSER_SETUP_LOADED=1
  -DST7789_DRIVER=1
  -DTFT_WIDTH=170
  -DTFT_HEIGHT=320
  -DCGRAM_OFFSET=1
  -DTFT_RGB_ORDER=TFT_BGR
  -DTFT_INVERSION_ON=1
  -DTFT_PARALLEL_8_BIT=1
  -DTFT_CS=6
  -DTFT_DC=7
  -DTFT_RST=5
  -DTFT_WR=8
  -DTFT_RD=9
  -DTFT_D0=39
  -DTFT_D1=40
  -DTFT_D2=41
  -DTFT_D3=42
  -DTFT_D4=45
  -DTFT_D5=46
  -DTFT_D6=47
  -DTFT_D7=48
  -DLOAD_GLCD=1
  -DLOAD_FONT2=1
  -DLOAD_FONT4=1
  -DSMOOTH_FONT=1
  -DSPI_FREQUENCY=27000000
lib_deps =
  bodmer/TFT_eSPI@2.5.43
  https://github.com/Bodmer/U8g2_for_TFT_eSPI.git#master
  tzapu/WiFiManager@2.0.17
  bblanchon/ArduinoJson@6.21.6

[env:native]
platform = native
lib_deps =
  bblanchon/ArduinoJson@6.21.6
test_build_src = false
```

- [ ] **Step 2: Add immutable hardware/timing constants**

```cpp
#pragma once
#include <cstdint>

namespace BuildConfig {
constexpr uint8_t PIN_POWER = 15;
constexpr uint8_t PIN_BUTTON_PREV = 0;
constexpr uint8_t PIN_BUTTON_NEXT = 14;
constexpr uint32_t DEFAULT_QUOTE_REFRESH_MS = 5000;
constexpr uint32_t MIN_QUOTE_REFRESH_MS = 3000;
constexpr uint32_t MAX_QUOTE_REFRESH_MS = 5000;
constexpr uint32_t INTRADAY_REFRESH_MS = 60000;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
constexpr uint32_t HTTP_CONNECT_TIMEOUT_MS = 1500;
constexpr uint32_t HTTP_READ_TIMEOUT_MS = 2500;
constexpr size_t HTTP_MAX_BODY_BYTES = 32768;
constexpr char CONFIG_NAMESPACE[] = "stockticker";
constexpr uint32_t CONFIG_SCHEMA_VERSION = 1;
}
```

- [ ] **Step 3: Add a compile-only placeholder main**

```cpp
#include <Arduino.h>
#include "build_config.h"

void setup() {
  pinMode(BuildConfig::PIN_POWER, OUTPUT);
  digitalWrite(BuildConfig::PIN_POWER, HIGH);
  Serial.begin(115200);
}

void loop() { delay(1000); }
```

- [ ] **Step 4: Add CI**

```yaml
name: ci
on:
  push:
  pull_request:
jobs:
  test-and-build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: '3.12'
      - run: pip install platformio
      - run: pio test -e native
      - run: pio run -e lilygo-t-display-s3
```

- [ ] **Step 5: Document upstream reuse and license obligations**

`THIRD_PARTY_NOTICES.md` must name:
- `Zaitronics/esp32-cyd-stock-ticker` — MIT; design/code reference for WiFiManager/web configuration.
- `dcluomax/stock-ticker-esp32c6` — MIT; design reference for setup flow and intraday display behavior.
- `Xinyuan-LilyGO/T-Display-S3` — MIT; authoritative hardware pinout/build reference.
- `Bodmer/TFT_eSPI`, `Bodmer/U8g2_for_TFT_eSPI`, `tzapu/WiFiManager`, `bblanchon/ArduinoJson` with their upstream license links/names.

- [ ] **Step 6: Verify toolchain**

Run:
```bash
pio test -e native
pio run -e lilygo-t-display-s3
```
Expected: native reports no test suites yet without build failure; firmware compiles successfully.

- [ ] **Step 7: Commit**

```bash
git add platformio.ini .github include src/main.cpp README.md THIRD_PARTY_NOTICES.md
git commit -m "chore: bootstrap T-Display-S3 stock ticker"
```

### Task 2: Define stock symbols, domain models, and numeric formatting

**Files:**
- Create: `lib/core/QuoteModels.h`
- Create: `lib/core/StockSymbol.h`
- Create: `lib/core/StockSymbol.cpp`
- Create: `lib/core/Formatters.h`
- Create: `lib/core/Formatters.cpp`
- Test: `test/test_symbol/test_main.cpp`
- Test: `test/test_formatters/test_main.cpp`

**Interfaces:**
- Produces `StockSymbol StockSymbol::parse(std::string_view raw)`.
- Produces `std::string eastMoneySecId() const` and `std::string tencentCode() const`.
- Produces `QuoteSnapshot`, `IntradayPoint`, `IntradaySeries`, `MarketStatus`, `ProviderId`.
- Produces `formatPrice`, `formatPercent`, `formatVolume`, `formatAmount`.

- [ ] **Step 1: Write failing symbol tests**

```cpp
#include <unity.h>
#include "StockSymbol.h"

void test_sh_symbol() {
  auto s = StockSymbol::parse("600519");
  TEST_ASSERT_TRUE(s.valid());
  TEST_ASSERT_EQUAL_STRING("600519.SH", s.canonical().c_str());
  TEST_ASSERT_EQUAL_STRING("1.600519", s.eastMoneySecId().c_str());
  TEST_ASSERT_EQUAL_STRING("sh600519", s.tencentCode().c_str());
}

void test_sz_symbol() {
  auto s = StockSymbol::parse("300750.SZ");
  TEST_ASSERT_EQUAL_STRING("0.300750", s.eastMoneySecId().c_str());
  TEST_ASSERT_EQUAL_STRING("sz300750", s.tencentCode().c_str());
}

void test_bse_symbol() {
  auto s = StockSymbol::parse("920047");
  TEST_ASSERT_EQUAL_STRING("920047.BJ", s.canonical().c_str());
  TEST_ASSERT_EQUAL_STRING("0.920047", s.eastMoneySecId().c_str());
  TEST_ASSERT_EQUAL_STRING("bj920047", s.tencentCode().c_str());
}

void test_reject_bad_symbol() {
  TEST_ASSERT_FALSE(StockSymbol::parse("ABC").valid());
  TEST_ASSERT_FALSE(StockSymbol::parse("60051").valid());
}
```

- [ ] **Step 2: Run tests and verify failure**

Run: `pio test -e native -f test_symbol`
Expected: FAIL because `StockSymbol` does not exist.

- [ ] **Step 3: Implement the value types**

```cpp
enum class Exchange { SSE, SZSE, BSE, UNKNOWN };
enum class MarketStatus { PRE_OPEN, TRADING_AM, LUNCH_BREAK, TRADING_PM, CLOSED, NON_TRADING_DAY, UNKNOWN };
enum class ProviderId { EAST_MONEY, TENCENT };

struct QuoteSnapshot {
  StockSymbol symbol;
  std::string name;
  double last = 0;
  double change = 0;
  double changePercent = 0;
  double open = 0;
  double high = 0;
  double low = 0;
  double prevClose = 0;
  uint64_t volume = 0;
  double amount = 0;
  uint64_t epochSeconds = 0;
  MarketStatus marketStatus = MarketStatus::UNKNOWN;
  ProviderId provider = ProviderId::EAST_MONEY;
};

struct IntradayPoint {
  uint16_t minuteOfDay = 0;
  float price = 0;
  float averagePrice = 0;
  uint32_t volume = 0;
};

using IntradaySeries = std::vector<IntradayPoint>;
```

Implement prefix rules:
- `60`, `68`, and `90` beginning with `9` when explicitly suffixed `.SH` -> SSE.
- `00`, `001`, `002`, `003`, `30` -> SZSE.
- current BSE codes beginning `4`, `8`, or `92` -> BSE.
- explicit `.SH`, `.SZ`, `.BJ` suffix overrides inferred exchange after validating six digits.

- [ ] **Step 4: Add failing formatter tests**

```cpp
TEST_ASSERT_EQUAL_STRING("1410.25", formatPrice(1410.25).c_str());
TEST_ASSERT_EQUAL_STRING("+1.23%", formatPercent(1.234).c_str());
TEST_ASSERT_EQUAL_STRING("-0.40%", formatPercent(-0.4).c_str());
TEST_ASSERT_EQUAL_STRING("123.4万手", formatVolume(1234000).c_str());
TEST_ASSERT_EQUAL_STRING("12.35亿", formatAmount(1235000000.0).c_str());
```

- [ ] **Step 5: Implement formatters and pass both suites**

Run:
```bash
pio test -e native -f test_symbol
pio test -e native -f test_formatters
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add lib/core test/test_symbol test/test_formatters
git commit -m "feat: add stock domain models and formatters"
```

### Task 3: Implement the A-share session clock

**Files:**
- Create: `lib/core/MarketClock.h`
- Create: `lib/core/MarketClock.cpp`
- Test: `test/test_market_clock/test_main.cpp`

**Interfaces:**
- Produces `MarketStatus MarketClock::status(const LocalDateTime&, bool providerHasTodayData) const`.
- Produces `uint32_t MarketClock::recommendedQuoteIntervalMs(MarketStatus) const`.

- [ ] **Step 1: Write failing state tests**

Use deterministic local-time structs, not `time()` inside the pure class:

```cpp
TEST_ASSERT_EQUAL(MarketStatus::PRE_OPEN,
  clock.status({2026,8,11,9,20,0,2}, true));
TEST_ASSERT_EQUAL(MarketStatus::TRADING_AM,
  clock.status({2026,8,11,9,30,0,2}, true));
TEST_ASSERT_EQUAL(MarketStatus::LUNCH_BREAK,
  clock.status({2026,8,11,12,0,0,2}, true));
TEST_ASSERT_EQUAL(MarketStatus::TRADING_PM,
  clock.status({2026,8,11,14,0,0,2}, true));
TEST_ASSERT_EQUAL(MarketStatus::CLOSED,
  clock.status({2026,8,11,15,5,0,2}, true));
TEST_ASSERT_EQUAL(MarketStatus::NON_TRADING_DAY,
  clock.status({2026,8,16,10,0,0,0}, false));
```

Weekday alone must not force `TRADING_*`: if provider has no current-day data, status degrades to `NON_TRADING_DAY` after a successful provider check.

- [ ] **Step 2: Verify failing test**

Run: `pio test -e native -f test_market_clock`
Expected: FAIL.

- [ ] **Step 3: Implement session boundaries**

Use China local time:
- pre-open: before 09:30 on weekday
- morning: `[09:30, 11:30]`
- lunch: `(11:30, 13:00)`
- afternoon: `[13:00, 15:00]`
- closed: after 15:00 when provider has current-day data
- weekend: non-trading
- weekday holiday inference: provider current-day absence => non-trading once data check completes

Recommended intervals:
- TRADING_AM/PM: configured 3000–5000ms
- PRE_OPEN/LUNCH_BREAK: 60000ms
- CLOSED/NON_TRADING_DAY: 300000ms
- UNKNOWN: 15000ms

- [ ] **Step 4: Run test and commit**

```bash
pio test -e native -f test_market_clock
git add lib/core/MarketClock* test/test_market_clock
git commit -m "feat: add A-share market session clock"
```

### Task 4: Implement and test EastMoney response parsing

**Files:**
- Create: `lib/providers/IQuoteProvider.h`
- Create: `lib/providers/EastMoneyParser.h`
- Create: `lib/providers/EastMoneyParser.cpp`
- Test: `test/test_eastmoney_parser/test_main.cpp`

**Interfaces:**
- `IQuoteProvider::fetchQuote(const StockSymbol&, QuoteSnapshot&) -> ProviderError`
- `IQuoteProvider::fetchIntraday(const StockSymbol&, IntradaySeries&) -> ProviderError`
- Pure parser functions:
  - `ProviderError EastMoneyParser::parseQuote(std::string_view body, const StockSymbol&, QuoteSnapshot&)`
  - `ProviderError EastMoneyParser::parseIntraday(std::string_view body, const StockSymbol&, IntradaySeries&)`

`ProviderError` values: `NONE`, `NETWORK`, `HTTP_STATUS`, `BODY_TOO_LARGE`, `PARSE`, `MISSING_FIELD`, `STALE`, `UNSUPPORTED`.

- [ ] **Step 1: Add a failing quote fixture test**

```cpp
const char* body = R"json({
  "data":{"f57":"600519","f58":"贵州茅台","f43":141025,
  "f44":142100,"f45":139800,"f46":140000,"f47":123456,
  "f48":1743210000.0,"f60":140000,"f169":1025,"f170":73,"f86":1786420800}
})json";
QuoteSnapshot q;
auto err = EastMoneyParser::parseQuote(body, StockSymbol::parse("600519"), q);
TEST_ASSERT_EQUAL(ProviderError::NONE, err);
TEST_ASSERT_DOUBLE_WITHIN(0.001, 1410.25, q.last);
TEST_ASSERT_DOUBLE_WITHIN(0.001, 10.25, q.change);
TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.73, q.changePercent);
TEST_ASSERT_EQUAL_STRING("贵州茅台", q.name.c_str());
```

Parser rule: if EastMoney numeric price fields are integer-like scaled values, divide `f43/f44/f45/f46/f60/f169` by 100; if API returns decimal numeric values, retain them. Implement one helper `decodePrice(JsonVariantConst)` and cover both cases.

- [ ] **Step 2: Add invalid payload tests**

Cases:
- missing `data`
- missing `f43`
- `f43="-"`
- last price negative
- malformed JSON
Expected: non-`NONE` error and no partially-valid snapshot exposed.

- [ ] **Step 3: Add a failing intraday fixture test**

```cpp
const char* body = R"json({"data":{"trends":[
"2026-08-11 09:30,1400.00,1400.00,1400.00,1400.00,1200,1680000,1400.00",
"2026-08-11 09:31,1401.20,1400.60,1401.20,1400.00,800,1120960,1400.60"
]}})json";
IntradaySeries points;
auto err = EastMoneyParser::parseIntraday(body, StockSymbol::parse("600519"), points);
TEST_ASSERT_EQUAL(ProviderError::NONE, err);
TEST_ASSERT_EQUAL_UINT32(2, points.size());
TEST_ASSERT_EQUAL_UINT16(571, points[1].minuteOfDay); // 09:31
TEST_ASSERT_FLOAT_WITHIN(0.01f, 1401.20f, points[1].price);
```

- [ ] **Step 4: Implement parser with hard bounds**

Rules:
- maximum 242 minute points; discard duplicates and out-of-order points.
- accept only 09:30–11:30 and 13:00–15:00.
- no dynamic unbounded accumulation.
- preserve UTF-8 name as returned.

- [ ] **Step 5: Verify and commit**

```bash
pio test -e native -f test_eastmoney_parser
git add lib/providers test/test_eastmoney_parser
git commit -m "feat: parse EastMoney quotes and intraday data"
```

### Task 5: Implement Tencent snapshot parsing and failover policy

**Files:**
- Create: `lib/providers/TencentParser.h`
- Create: `lib/providers/TencentParser.cpp`
- Create: `lib/core/ProviderFailover.h`
- Create: `lib/core/ProviderFailover.cpp`
- Test: `test/test_tencent_parser/test_main.cpp`
- Test: `test/test_failover/test_main.cpp`

**Interfaces:**
- `TencentParser::parseQuote(std::string_view, const StockSymbol&, QuoteSnapshot&)`.
- `ProviderFailover::recordSuccess(ProviderId, uint64_t nowMs)`.
- `ProviderFailover::recordFailure(ProviderId, uint64_t nowMs)`.
- `ProviderId ProviderFailover::activeProvider(uint64_t nowMs) const`.

- [ ] **Step 1: Write a Tencent parser test using the `~`-delimited response**

Construct fixture with documented positional fields used by implementation:

```cpp
const char* body =
"v_sh600519=\"1~贵州茅台~600519~1410.25~1400.00~1400.00~123456~...\";";
```

The test must assert: name, symbol, last, previous close, open, volume, high, low, change, percent. Keep field index constants in one parser table; never scatter numeric indexes through provider code.

- [ ] **Step 2: Write failover tests**

Policy:
- start on EastMoney.
- 1 or 2 consecutive EastMoney failures: remain EastMoney.
- 3 consecutive failures inside 60 seconds: switch snapshot requests to Tencent.
- while on Tencent, attempt an EastMoney probe no more often than every 120 seconds.
- require 2 consecutive successful EastMoney probes before switching back.
- intraday remains EastMoney-only in V1; on primary failure keep last successful chart.

- [ ] **Step 3: Implement parser and state machine**

`ProviderFailover` must be pure C++ with no `millis()` dependency; caller passes time.

- [ ] **Step 4: Verify and commit**

```bash
pio test -e native -f test_tencent_parser
pio test -e native -f test_failover
git add lib/providers/TencentParser* lib/core/ProviderFailover* test/test_tencent_parser test/test_failover
git commit -m "feat: add Tencent fallback and provider failover"
```

### Task 6: Implement application configuration, JSON codec, and NVS persistence

**Files:**
- Create: `lib/core/AppConfig.h`
- Create: `lib/core/AppConfig.cpp`
- Create: `src/device/ConfigStore.h`
- Create: `src/device/ConfigStore.cpp`
- Test: `test/test_app_config/test_main.cpp`

**Interfaces:**
- `AppConfig AppConfig::defaults()`.
- `ConfigValidationResult validate(const AppConfig&)`.
- `bool AppConfigCodec::encode(const AppConfig&, std::string&)`.
- `bool AppConfigCodec::decode(std::string_view, AppConfig&)`.
- `ConfigStore::load(AppConfig&)`, `save(const AppConfig&)`, `clearAppConfig()`.

- [ ] **Step 1: Write config validation tests**

Rules:
- exactly 3–5 enabled stocks.
- each symbol must normalize successfully.
- symbols must be unique after canonicalization.
- display name UTF-8 byte length max 30; empty name is allowed and means use provider name.
- refresh interval must be integer seconds 3, 4, or 5.
- schema version must be `1` after decode/migration.

- [ ] **Step 2: Write codec round-trip test**

Example config:

```json
{
  "schema":1,
  "quote_refresh_sec":5,
  "stocks":[
    {"symbol":"600519.SH","name":"贵州茅台"},
    {"symbol":"300750.SZ","name":"宁德时代"},
    {"symbol":"002594.SZ","name":"比亚迪"}
  ]
}
```

Assert encode→decode preserves canonical symbols and refresh interval.

- [ ] **Step 3: Implement pure codec/validation and run native tests**

Run: `pio test -e native -f test_app_config`
Expected: PASS.

- [ ] **Step 4: Implement Preferences adapter**

Store the encoded JSON under namespace `stockticker`, key `app_config`. On missing/corrupt config, return `false` without erasing Wi-Fi credentials; caller decides to enter provisioning.

- [ ] **Step 5: Build firmware and commit**

```bash
pio run -e lilygo-t-display-s3
git add lib/core/AppConfig* src/device/ConfigStore* test/test_app_config
git commit -m "feat: persist validated stock configuration"
```

### Task 7: Implement bounded HTTP transports and live providers

**Files:**
- Create: `src/network/HttpTransport.h`
- Create: `src/network/HttpTransport.cpp`
- Create: `src/network/EastMoneyProvider.h`
- Create: `src/network/EastMoneyProvider.cpp`
- Create: `src/network/TencentProvider.h`
- Create: `src/network/TencentProvider.cpp`

**Interfaces:**
- `HttpResponse HttpTransport::get(const std::string& url, const HttpHeaders&)`.
- EastMoney provider implements both `fetchQuote` and `fetchIntraday`.
- Tencent provider implements `fetchQuote`; `fetchIntraday` returns `ProviderError::UNSUPPORTED`.

- [ ] **Step 1: Implement HTTP body bounding**

`HttpTransport` must:
- set connect timeout 1500ms and read timeout 2500ms.
- reject `Content-Length > 32768` before reading.
- if unknown length, stream into a buffer and abort once 32768 bytes is exceeded.
- accept only HTTP 200.
- set `User-Agent: TDisplayGP/1.0` and EastMoney `Referer: https://quote.eastmoney.com/`.
- for HTTPS use `WiFiClientSecure::setInsecure()` and document why in README.

- [ ] **Step 2: Implement exact EastMoney URLs**

Snapshot:
```text
https://push2.eastmoney.com/api/qt/stock/get?secid={SECID}&fields=f57,f58,f43,f44,f45,f46,f47,f48,f60,f86,f169,f170
```

Intraday:
```text
https://push2his.eastmoney.com/api/qt/stock/trends2/get?secid={SECID}&fields1=f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11&fields2=f51,f52,f53,f54,f55,f56,f57,f58&ndays=1&iscr=0&iscca=0
```

- [ ] **Step 3: Implement Tencent URL**

```text
https://qt.gtimg.cn/q={TENCENT_CODE}
```

- [ ] **Step 4: Compile**

Run: `pio run -e lilygo-t-display-s3`
Expected: PASS with no parser/network type mismatch.

- [ ] **Step 5: Commit**

```bash
git add src/network
git commit -m "feat: add live A-share market data providers"
```

### Task 8: Implement Wi-Fi provisioning and LAN settings Web UI

**Files:**
- Create: `src/network/ProvisioningService.h`
- Create: `src/network/ProvisioningService.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- `bool ProvisioningService::ensureConnected(AppConfig&)` for boot-time captive portal.
- `void ProvisioningService::beginWebPortal(AppConfig&)` after STA connects.
- `void ProvisioningService::process()` from main loop.
- POST `/api/config` validates and saves stock list/refresh interval.
- POST `/api/wifi/reconfigure` schedules restart into WiFiManager portal.
- GET `/api/status` returns JSON with IP/RSSI/uptime/config summary.

- [ ] **Step 1: Build first-boot portal fields**

Use WiFiManager 2.0.17 custom parameters:
- `stock1`…`stock5`
- `name1`…`name5`
- `refresh` with default `5`

AP name: `TDisplay-GP-Setup`.
AP password: none in V1 to minimize setup friction; portal is only present when unconfigured/reconfiguration is explicitly requested.

- [ ] **Step 2: On save, validate before persisting**

Reject fewer than 3 or more than 5 valid unique symbols. When invalid, keep portal active and present a clear message rather than booting into a broken app.

- [ ] **Step 3: Implement normal LAN settings page**

Serve a mobile-first single HTML page directly from Flash (`PROGMEM`) with:
- IP/Wi-Fi status
- five stock rows (symbol + optional display name)
- refresh select: 3/4/5 seconds
- Save button
- “更换 Wi-Fi” button

No external JS/CSS/CDN dependencies.

- [ ] **Step 4: Implement reconfiguration flow**

`POST /api/wifi/reconfigure` sets Preferences key `force_portal=1` and restarts. At next boot, WiFiManager starts config portal even if credentials exist; after successful connection clear `force_portal`.

- [ ] **Step 5: Compile and commit**

```bash
pio run -e lilygo-t-display-s3
git add src/network/ProvisioningService* src/main.cpp
git commit -m "feat: add captive portal and phone settings UI"
```

### Task 9: Implement the T-Display-S3 device layer and non-blocking buttons

**Files:**
- Create: `src/device/DeviceLayer.h`
- Create: `src/device/DeviceLayer.cpp`

**Interfaces:**
- `void DeviceLayer::begin()`.
- `ButtonEvent DeviceLayer::pollButtons(uint32_t nowMs)` where event is `NONE`, `PREVIOUS`, `NEXT`.
- `bool DeviceLayer::wifiConnected() const`.
- `bool DeviceLayer::timeSynchronized() const`.
- `LocalDateTime DeviceLayer::localDateTime() const`.
- exposes `TFT_eSPI& display()` and `U8g2_for_TFT_eSPI& unicodeFont()` to renderer only.

- [ ] **Step 1: Initialize hardware in safe order**

Order:
1. GPIO15 HIGH.
2. GPIO0/GPIO14 `INPUT_PULLUP`.
3. TFT `init()` and portrait rotation for 170×320.
4. U8g2 bridge `begin(tft)`.
5. NTP after Wi-Fi, timezone `CST-8` for Asia/Shanghai.

- [ ] **Step 2: Implement edge/debounce state machine**

A button event fires once on stable HIGH→LOW transition held at least 40ms. Holding a button does not repeat in V1.

- [ ] **Step 3: Add a device smoke screen**

Before full app integration, render:
```text
T-Display GP
屏幕 OK
BTN0 / BTN14
Wi-Fi: ...
```
Use `u8g2_font_wqy12_t_gb2312` or equivalent GB2312-capable U8g2 font to prove Chinese rendering.

- [ ] **Step 4: Compile and commit**

```bash
pio run -e lilygo-t-display-s3
git add src/device/DeviceLayer*
git commit -m "feat: add T-Display-S3 hardware layer"
```

### Task 10: Implement the asynchronous market-data worker

**Files:**
- Create: `src/network/MarketDataWorker.h`
- Create: `src/network/MarketDataWorker.cpp`

**Interfaces:**

```cpp
enum class MarketRequestType { QUOTE, INTRADAY, PRIMARY_PROBE };
struct MarketRequest { uint32_t requestId; MarketRequestType type; StockSymbol symbol; ProviderId provider; };
struct MarketResult { uint32_t requestId; MarketRequestType type; ProviderError error; QuoteSnapshot quote; IntradaySeries intraday; };
```

- `bool enqueue(const MarketRequest&)` must return immediately.
- `bool tryReceive(MarketResult&)` must return immediately.
- worker owns provider network calls; main task never invokes `HTTPClient` directly.

- [ ] **Step 1: Create FreeRTOS queues**

Queue depths:
- request queue: 8
- result queue: 8

Worker stack: 12288 bytes, priority 1, pinned to core 0. UI Arduino loop remains on its normal core.

- [ ] **Step 2: Deduplicate pending work**

Do not enqueue a second quote request for the same symbol/type while one is already pending. A stock switch may enqueue the new symbol immediately; old result may arrive later and is stored in cache but must not overwrite current-screen selection.

- [ ] **Step 3: Bound result payloads**

Intraday max 242 points. Ensure queue payload does not copy huge dynamic structures blindly: allocate result objects on heap in worker and pass pointers through queues; receiver takes ownership and deletes after merge. Add RAII helper so failure paths do not leak.

- [ ] **Step 4: Compile and commit**

```bash
pio run -e lilygo-t-display-s3
git add src/network/MarketDataWorker*
git commit -m "feat: isolate market requests from UI loop"
```

### Task 11: Implement StockController scheduling, cache, switching, and failover

**Files:**
- Create: `src/app/StockController.h`
- Create: `src/app/StockController.cpp`

**Interfaces:**
- `void begin(const AppConfig&)`.
- `void tick(uint32_t nowMs, const LocalDateTime&)`.
- `void onButton(ButtonEvent)`.
- `void consumeMarketResults()`.
- `const StockViewModel& viewModel() const`.
- `bool takeDirtyFlag()`.

Cache per stock:
```cpp
struct StockCacheEntry {
  QuoteSnapshot quote;
  IntradaySeries intraday;
  bool hasQuote = false;
  bool hasIntraday = false;
  uint32_t quoteUpdatedMs = 0;
  uint32_t intradayUpdatedMs = 0;
};
```

- [ ] **Step 1: Add host-testable scheduler/failover seam**

Keep scheduling math and provider-selection policy in pure helpers already tested. Controller depends on a small `IMarketDataQueue` interface so a fake can be used later if regression tests are needed.

- [ ] **Step 2: Implement stock switching**

GPIO0 event decrements index wrapping `0 -> N-1`; GPIO14 increments wrapping `N-1 -> 0`. On switch:
1. immediately publish cached view model.
2. if cache is older than configured quote interval, enqueue quote.
3. if intraday older than 60s, enqueue intraday.

- [ ] **Step 3: Implement session scheduling**

During TRADING_AM/PM enqueue current-stock quote every configured 3–5s. To keep all 3–5 symbols reasonably fresh, enqueue one non-current stock quote per cycle in round-robin only when queue capacity exists; never burst all five at once.

- [ ] **Step 4: Integrate failover**

EastMoney quote failures feed `ProviderFailover`. When active provider becomes Tencent, quote requests use Tencent, while intraday stays cached. Probe EastMoney per policy and switch back only after two successful probes.

- [ ] **Step 5: Handle stale/offline state**

Cached data stays visible. View model includes `dataAgeSeconds`, `wifiOnline`, `provider`, and `errorBadge` so UI can show “离线” or “数据异常” without clearing values.

- [ ] **Step 6: Compile and commit**

```bash
pio run -e lilygo-t-display-s3
git add src/app
git commit -m "feat: add stock controller and resilient cache"
```

### Task 12: Implement the 170×320 stock screen and intraday chart

**Files:**
- Create: `src/ui/UiTheme.h`
- Create: `src/ui/StockScreen.h`
- Create: `src/ui/StockScreen.cpp`

**Interfaces:**
- `void StockScreen::begin(TFT_eSPI&, U8g2_for_TFT_eSPI&)`.
- `void StockScreen::render(const StockViewModel&, bool fullRedraw)`.

- [ ] **Step 1: Lock the exact portrait layout**

Coordinates for 170×320:
- header `y=0..28`: name/code + market badge
- main price `y=30..86`: large last price, change, percent
- metrics `y=88..142`: 2×2 grid open/high/low/prev close
- turnover `y=144..166`: volume + amount
- chart `x=8..162, y=172..292`
- footer `y=296..319`: `2/5`, Wi-Fi/data/provider badge

- [ ] **Step 2: Implement A-share color semantics**

- positive: red
- negative: green
- zero/unknown: light gray
- chart line: neutral cyan/white, not red/green, so trend line is visible regardless of sign
- background: black

Keep colors in `UiTheme.h`; no business logic in renderer.

- [ ] **Step 3: Implement chart transform**

Chart y-range includes min/max of intraday price plus previous close. If range < 0.2% of prev close, force minimum ±0.1% padding. Draw prev-close horizontal guide. X maps fixed A-share trading minutes so lunch break appears as a small discontinuity, not compressed into a misleading continuous minute sequence.

- [ ] **Step 4: Implement partial redraw**

Full redraw on stock switch/layout state change. On quote update only repaint price/metrics/footer rectangles. On intraday update repaint chart rectangle. Avoid `fillScreen()` every 5 seconds.

- [ ] **Step 5: Verify Chinese font and long names**

Render provider names through U8g2 GB2312 font. If display name width exceeds header region, truncate by UTF-8 codepoint to fit and append `…`; never split UTF-8 bytes.

- [ ] **Step 6: Compile and commit**

```bash
pio run -e lilygo-t-display-s3
git add src/ui
git commit -m "feat: render A-share quote and intraday screen"
```

### Task 13: Integrate the application lifecycle

**Files:**
- Modify: `src/main.cpp`
- Modify: `README.md`

**Interfaces:**
- Main loop has no blocking delay longer than 5ms.

- [ ] **Step 1: Wire boot sequence**

```cpp
setup():
  device.begin();
  configStore.load(config);
  provisioning.ensureConnected(config);
  sync NTP;
  provisioning.beginWebPortal(config);
  dataWorker.begin(eastMoney, tencent);
  controller.begin(config);
  screen.begin(device.display(), device.unicodeFont());
```

If app config is missing/invalid, provisioning must run even if Wi-Fi credentials already exist.

- [ ] **Step 2: Wire loop sequence**

```cpp
loop():
  provisioning.process();
  event = device.pollButtons(millis());
  controller.onButton(event);
  controller.consumeMarketResults();
  controller.tick(millis(), device.localDateTime());
  if (controller.takeDirtyFlag()) screen.render(controller.viewModel(), ...);
  delay(1);
```

- [ ] **Step 3: Add configuration hot-apply policy**

Stock/refresh changes save then trigger controlled ESP restart so all modules reload a coherent config. Wi-Fi change triggers the dedicated portal restart. Do not implement partial runtime mutation in V1.

- [ ] **Step 4: Run all automated checks**

```bash
pio test -e native
pio run -e lilygo-t-display-s3
```
Expected: all native suites PASS; firmware builds.

- [ ] **Step 5: Commit**

```bash
git add src/main.cpp README.md
git commit -m "feat: integrate T-Display-S3 stock ticker application"
```

### Task 14: Hardware acceptance, API contract checks, and release documentation

**Files:**
- Create: `docs/hardware-acceptance.md`
- Create: `docs/api-contract.md`
- Modify: `README.md`

**Interfaces:**
- Produces a reproducible physical-device acceptance checklist and documents live provider assumptions.

- [ ] **Step 1: Flash and perform board bring-up**

Commands:
```bash
pio run -e lilygo-t-display-s3 -t upload
pio device monitor -b 115200
```
Verify:
- screen powers on
- no boot loop
- both buttons generate exactly one event per press
- Chinese text is legible

- [ ] **Step 2: Factory-reset test**

Erase flash once:
```bash
pio run -e lilygo-t-display-s3 -t erase
pio run -e lilygo-t-display-s3 -t upload
```
Acceptance:
- AP `TDisplay-GP-Setup` appears
- phone captive portal opens, or `192.168.4.1` works
- three stocks + Wi-Fi can be saved without serial console

- [ ] **Step 3: Live API contract test with three exchanges**

Configure one symbol from each supported exchange where live data is available:
- SSE example `600519`
- SZSE example `300750`
- BSE example `920047`

Record actual EastMoney/Tencent responses in `docs/api-contract.md` without copying excessive unrelated response fields. If a provider does not support BSE using the planned mapping, keep BSE on EastMoney only and mark Tencent fallback capability per exchange in code rather than rejecting the stock.

- [ ] **Step 4: Trading behavior acceptance**

During an actual session or with controlled clock/provider fixture mode verify:
- quote refresh every configured 3–5s
- intraday refresh independent at ~60s
- lunch stops high-frequency polling
- close retains final quote/chart and displays 已收盘
- next trading session resumes without manual action

- [ ] **Step 5: Resilience acceptance**

Verify:
- disable Wi-Fi for 2 minutes: UI/buttons remain responsive and cached values remain
- restore Wi-Fi: automatic refresh resumes
- force EastMoney hostname failure in a test build: after 3 failures snapshot falls back to Tencent
- EastMoney restore: two successful probes return primary provider
- restart: Wi-Fi and stock pool persist

- [ ] **Step 6: Final automated verification and release tag candidate**

```bash
pio test -e native
pio run -e lilygo-t-display-s3
git status --short
```
Expected: tests PASS, build PASS, working tree clean.

- [ ] **Step 7: Commit docs**

```bash
git add docs README.md
git commit -m "docs: add hardware acceptance and provider contracts"
```

## Plan Self-Review

### Spec coverage

- 3–5 A-share stocks: Tasks 2, 6, 8, 11.
- 3–5 second quote refresh: Tasks 3, 6, 11.
- intraday chart: Tasks 4, 7, 10, 12.
- physical buttons: Tasks 9, 11.
- captive portal + phone settings: Task 8.
- close/lunch/next-session behavior: Tasks 3, 11, 14.
- EastMoney primary + Tencent fallback: Tasks 4, 5, 7, 11.
- offline/non-blocking behavior: Tasks 7, 10, 11, 14.
- persistent config: Tasks 6, 8, 14.
- Chinese names: Tasks 9, 12, 14.
- open-source reuse/attribution: Task 1.

### Placeholder scan

No `TBD`, `TODO`, “implement later”, or unspecified error-handling steps remain. BSE fallback uncertainty is explicitly converted into an acceptance-time capability check with a defined degradation rule: EastMoney-only for BSE if Tencent does not serve the planned `bj` code.

### Type consistency

`StockSymbol`, `QuoteSnapshot`, `IntradaySeries`, `ProviderError`, `ProviderId`, `MarketStatus`, and queue request/result types are defined before their consumers. Provider methods and controller/worker boundaries use the same domain types throughout.
