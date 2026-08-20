# T-Display GP

基于 **LILYGO T-Display-S3** 的 320×170 多应用桌面信息终端。

当前菜单：

```text
股票 → 天气 → 辉光时钟 → 智能家居 → 加密货币 → 设备信息
```

> GitHub `dgcary/t-display-gp` 是项目唯一事实源。开发、编译、Artifact 和 Codex 真机部署都必须对应明确 Git SHA。

## 启动 / 待机

- **开机默认进入辉光时钟。**
- Stock 和 NixieClock 永久豁免自动待机。
- Menu / Weather / Home Assistant / Crypto / DeviceInfo 连续 **30 秒无有效按键**自动进入 NixieClock。
- 任意有效 GPIO0/GPIO14 short/long 重新计时；网络刷新/数据返回不算用户活动。
- idle 由 `AppManager` 统一实现，使用 wrap-safe `millis()`。

## 按键

```text
普通 App:
GPIO0 short  previous
GPIO14 short next
GPIO0 long   menu
GPIO14 long  reserved/no-op

Menu:
GPIO0 short  previous app
GPIO14 short next app
GPIO0 long   no-op
GPIO14 long  enter
```

40 ms debounce，700 ms long，长按释放不再触发 short。

## Stock

- 腾讯 quote + intraday primary，EastMoney fallback。
- Quote/Intraday health 独立。
- Quote 优先于 intraday；waiting intraday latest-wins。
- Tencent intraday 有界 deferred retry，完整失败等待 normal refresh。
- quote 连续失败阈值触发 EastMoney；备用期间 probe Tencent，2 次连续 probe success 恢复。
- error 保留 last-valid cache。
- **Stock 不受 30 秒 idle 影响。**

## Weather

Open-Meteo 当前天气 + 3 日预报。地点/经纬度/刷新通过现有 Web/Captive Portal 配置。默认 15 min，5–60 min；active-only；failure 保留 cache；手绘水彩风小猫轻量动画。

## Nixie Clock

- **默认启动 + global idle destination。**
- 使用公共 NTP 后的 ESP32 local system clock，不新增 NTP worker。
- HH:MM、日期、星期、秒数；未同步 fail-closed。
- 1 s sample，约 500 ms colon animation；局部刷新。
- local-only，不使用 HttpTransport/AppDataWorker/NetworkArbiter。

## Home Assistant

### 角色

**用户现有的 Home Assistant 是服务器；T-Display-S3 只是 REST API 客户端。**

LILYGO 上不会再运行第二套 Home Assistant。V1 只读：

```text
T-Display -> GET <HA>/api/states/<entity_id> -> existing HA Server
```

不调用 `/api/services`，不做灯/门锁/空调写控制。

### Dashboard

- 1–4 entities，可选 labels。
- sequential fetch。
- refresh 30–300 s，default 30 s。
- per-entity last-valid cache。
- active-only。
- Bearer Long-Lived Access Token。

### HTTP / HTTPS

兼容现有 LAN HA：

```text
http://homeassistant.local:8123
http://192.168.x.x:8123
```

HTTP 无需 CA，但 Token 在 LAN 明文传输，只适合可信 LAN。

HTTPS：

```text
https://ha.example.com
```

必须提供 CA PEM；使用 `WiFiClientSecure::setCACert()`，**HA HTTPS 禁止 `setInsecure()`**。

### T-Display HA 配置页

```text
http://<T-Display-IP>:8081/
```

填写 existing HA Server URL、Token、1–4 entity IDs、labels、refresh 和 HTTPS CA。

**`:8081` 是 T-Display 自己的配置页，不是 HA Server。** `/api/ha/status` 只返回 `ha_token_set` / `ha_ca_set`，不回显 secrets；serial 也不记录 Token。

## Crypto

- BTC / ETH / SOL vs USDT。
- Binance market-data-only `data-api.binance.vision`。
- 一次 `/api/v3/ticker/24hr` 获取 BTCUSDT/ETHUSDT/SOLUSDT。
- 无 Binance API Key。
- price + 24h change；60 s；active-only。
- parser 按 symbol 映射；duplicate/missing/malformed fail-closed；failure 保留 last complete snapshot。

## DeviceInfo

LAN IP、SSID/RSSI/MAC、uptime/time、heap/min heap、PSRAM、`Web: http://<IP>/`。local-only，不显示 secret。

## Architecture

```text
AppManager
├── StockApp -> dedicated MarketDataWorker
├── WeatherApp ┐
├── HomeAssistantApp ├-> one shared AppDataWorker
├── CryptoApp ┘       -> typed result queues
├── NixieClockApp (local-only)
└── DeviceInfoApp (local-only)

all actual external HTTP/TLS -> NetworkArbiter -> max one operation
```

Weather/HA/Crypto 共享一个 worker；typed result queues 防止 cross-app late-result loss。FreeRTOS queue 传 C++ object pointers，不 raw-copy 含 `std::string` 的对象。

## Transport / security

Public Stock/Weather/Crypto `HttpTransport`：connect 1500 ms，read setting 2500 ms，TLS handshake cap 5 s，body max 32 KiB，reuse false。

HA credential path：

```text
http://  -> WiFiClient / trusted-LAN cleartext
https:// -> WiFiClientSecure + setCACert / no insecure fallback
```

HA response body max 4 KiB；两种 HA mode 都经过 NetworkArbiter。

## Config

Stock/Weather AppConfig 保持 schema v2 + `stockticker` namespace。HA 使用独立 `ha_config` NVS blob；normal app-only firmware upgrade 不擦 NVS。

## Diagnostics

```text
[md]      Stock
[appdata] WEATHER / HOME_ASSISTANT / CRYPTO
[net]     actual network; HA mode=HA_HTTP or HA_CA
[sys]     MENU/STOCK/WEATHER/NIXIE_CLOCK/HOME_ASSISTANT/CRYPTO/DEVICE_INFO
```

不得记录 HA Token/Wi-Fi password 等 secret。

## Required verification

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_nixie_clock_contract.py
python tools/validate_dashboard_apps_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
```

CI 同时跑 Ubuntu/Windows native；ESP build 成功后上传 exact-SHA Artifact，包含 firmware/partitions/bootloader/manifest。

## Codex

Codex 只负责 exact-SHA Artifact 下载/hash 校验、按 manifest offset 烧 `firmware.bin`、115200 serial、真机 UI/network/idle/soak 测试并回传证据。正常不编译、不改源码、不 erase NVS、不重写 partition/bootloader。

详细：`docs/deployment.md`、`docs/api-contract.md`、`docs/hardware-acceptance.md`。
