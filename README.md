# T-Display GP

基于 **LILYGO T-Display-S3** 的 320×170 多应用桌面信息终端。当前固件包含：

```text
股票 → 天气 → 辉光时钟 → 智能家居 → 加密货币 → 设备信息
```

> GitHub `dgcary/t-display-gp` 是项目唯一事实源。开发、编译、Artifact 和 Codex 真机部署都必须对应明确的 Git SHA。

## 启动与待机

- **开机默认进入辉光时钟。**
- `股票`：不触发自动待机，停留多久都保持股票页面。
- `辉光时钟`：待机目标，本身不重复触发。
- `主菜单 / 天气 / 智能家居 / 加密货币 / 设备信息`：连续 **30 秒没有任何有效按键事件**，自动切回辉光时钟。
- GPIO0/GPIO14 的有效短按或长按都会重新开始 30 秒计时；网络刷新、行情变化、HA/Crypto 数据返回不算用户操作。
- idle 逻辑集中由 `AppManager` 管理，并使用 wrap-safe `millis()` 差值。

## 按键

普通 App：

```text
GPIO0 短按  -> 当前 App 上一项
GPIO14 短按 -> 当前 App 下一项
GPIO0 长按  -> 主菜单
GPIO14 长按 -> 保留 / no-op
```

主菜单：GPIO0/GPIO14 短按前后选择，GPIO14 长按进入，GPIO0 长按 no-op。Debounce 40 ms；长按 700 ms；长按释放不再产生短按。

## 股票 App

- 沪/深/北 A 股，3–5 只。
- 腾讯报价+分时主源，东方财富备用。
- Quote / Intraday 健康与 fallback 独立。
- Quote 优先于 intraday；等待分时 latest-wins。
- 腾讯分时有限重试，完整失败等待正常刷新。
- 3 次腾讯 quote failure 切 EastMoney；备用期间 probe Tencent，连续 2 次成功恢复。
- 网络失败保留最后有效缓存。
- **股票页不受 30 秒 idle 影响。**

## 天气 App

Open-Meteo 当前天气 + 3 日预报。地点/经纬度/刷新通过现有 Web/Captive Portal 配置。默认 15 min，可配置 5–60 min；active-only；失败保留 cache；右侧为轻量程序化手绘水彩风小猫动画。

## 辉光时钟 App

- **默认启动 App，也是 30 秒 idle destination。**
- 复用公共 NTP 后的 ESP32 local system time，不新增 NTP worker。
- `HH:MM`、日期、星期、秒数；未同步 fail-closed。
- 每秒采样；冒号约 500 ms；局部刷新，无周期整屏清屏。
- local-only，不使用 `HttpTransport` / `AppDataWorker` / `NetworkArbiter`。

## Home Assistant / 智能家居 App

### 角色非常明确

**你现有的 Home Assistant 是服务器；T-Display-S3 只是 REST API 客户端。**

固件不会在 LILYGO 上运行第二套 Home Assistant，也不会要求替换现有服务器。数据流：

```text
T-Display-S3
  -> GET /api/states/<entity_id>
  -> 你现有的 Home Assistant Server
```

V1 是 **只读 Dashboard**，不调用 `/api/services`，不执行灯/门锁/空调控制。

### 能力

- 1–4 个 entity ID，可选显示名。
- 顺序读取，默认 30 s，配置范围 30–300 s。
- per-entity last-valid cache。
- active-only 调度。
- `Authorization: Bearer <Long-Lived Access Token>`。

### HTTP / HTTPS

兼容常见已有 HA LAN 地址：

```text
http://homeassistant.local:8123
http://192.168.x.x:8123
```

HTTP 不需要 CA，但 Bearer Token 在 LAN 明文传输，因此只适合可信局域网。

若现有 HA 已启用 HTTPS，则配置 `https://...` 和 CA PEM。固件使用 `WiFiClientSecure::setCACert()` 严格验证，**HA HTTPS 禁止 `setInsecure()`**。

### T-Display HA 客户端配置页

```text
http://<T-Display-IP>:8081/
```

这里填写已有 HA Server URL、Token、1–4 个 entity ID、label、refresh，以及 HTTPS 时的 CA。

**`:8081` 只是 T-Display 自己的配置页面，不是 Home Assistant Server。** 页面本身为局域网 HTTP，只在可信 LAN 使用。

`/api/ha/status` 只返回 `ha_token_set` / `ha_ca_set` 布尔状态，不回显 Token 或 CA；串口也不记录 Token。

## Crypto / 加密货币 App

- BTC / ETH / SOL 对 USDT。
- Binance 专用只读市场数据域 `data-api.binance.vision`。
- 一次 `/api/v3/ticker/24hr` 取得三个 symbol。
- 无 Binance API Key。
- 最新价 + 24h change；60 s refresh；active-only。
- parser 按 symbol 映射，重复/缺失/非法 fail-closed；失败保留最后完整 snapshot。

## 设备信息 App

LAN IP、SSID/RSSI/MAC、uptime/local time、heap/min heap、PSRAM、`Web: http://<IP>/`。local-only，不显示 secret。

## 架构

```text
AppManager
├── StockApp -> MarketDataWorker
├── WeatherApp ┐
├── HomeAssistantApp ├-> single shared AppDataWorker
├── CryptoApp ┘       -> typed result queues
├── NixieClockApp (local-only)
└── DeviceInfoApp (local-only)

actual external network -> NetworkArbiter -> at most one operation at once
```

Weather / HA / Crypto 共用 **一个** AppDataWorker，结果按 `AppDataRequestType` 分队列，避免互相消费迟到结果。FreeRTOS 队列传 C++ 对象指针，不 byte-copy 含 `std::string` 的非平凡对象。

## 网络安全/Transport

公共行情、Weather、Crypto 继续现有 `HttpTransport`：connect 1500 ms、read setting 2500 ms、TLS handshake 5 s、body max 32 KiB、reuse false。

HA credentialed transport：

```text
http://  -> WiFiClient；可信 LAN 明文模式
https:// -> WiFiClientSecure + setCACert；禁止 insecure TLS
```

HA 单实体 body max 4 KiB；两种 HA 模式都经过 NetworkArbiter。

## 配置

股票/天气 AppConfig 保持 **schema v2** 和 `stockticker` NVS namespace。HA 使用单独 `ha_config` NVS blob；正常 application firmware 升级不擦 NVS。

## Diagnostics

```text
[md]      Stock
[appdata] WEATHER / HOME_ASSISTANT / CRYPTO
[net]     actual network; HA mode=HA_HTTP or HA_CA
[sys]     MENU/STOCK/WEATHER/NIXIE_CLOCK/HOME_ASSISTANT/CRYPTO/DEVICE_INFO
```

不得记录 HA Token、Wi-Fi password 等 secret。

## 开发侧强制验证

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_nixie_clock_contract.py
python tools/validate_dashboard_apps_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
```

CI 同时跑 Ubuntu/Windows native，ESP build 成功后上传 `tdisplay-gp-firmware-<SOURCE_SHA>`，含 firmware/partitions/bootloader/manifest。

## Codex

Codex 只负责 exact-SHA artifact 下载、manifest/hash 校验、烧 `firmware.bin` 到 manifest app offset、115200 serial、真机 UI/网络/idle/soak 测试并回传证据。正常不编译、不改源、不 erase NVS、不重写 partition/bootloader。

完整部署见 `docs/deployment.md`；Provider 契约见 `docs/api-contract.md`；真机验收见 `docs/hardware-acceptance.md`。
