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

主菜单：

```text
GPIO0 短按  -> 上一个 App
GPIO14 短按 -> 下一个 App
GPIO0 长按  -> no-op
GPIO14 长按 -> 进入选中 App
```

Debounce 40 ms；长按阈值 700 ms；长按触发后释放不会再产生一次短按。

## 股票 App

- 沪/深/北 A 股，股票池 3–5 只。
- 腾讯：报价 + 分时主源。
- 东方财富：报价 + 分时备用源。
- Quote / Intraday 健康与 fallback 独立。
- 报价优先级高于分时；等待分时 latest-wins。
- 腾讯分时 transient retry 有界，最终失败才切东方财富；完整分时周期失败后等待正常刷新间隔。
- 连续 3 次腾讯报价失败切东方财富；备用期间探测腾讯，连续 2 次恢复探测成功后回腾讯。
- 网络失败保留最后有效报价/分时缓存。
- **股票页不受 30 秒自动回时钟影响。**

## 天气 App

- Open-Meteo 当前天气 + 3 日预报。
- 地点名称、经纬度、刷新周期通过现有 Web/Captive Portal 配置。
- 默认 15 分钟刷新，可配置 5–60 分钟。
- 失败保留最后有效缓存，不 tight retry。
- 仅前台主动调度。
- 右侧使用轻量程序化手绘水彩风小猫与两帧天气动画。

## 辉光时钟 App

- **默认启动 App，也是系统 30 秒 idle destination。**
- 使用公共 NTP 同步后的 ESP32 本地系统时间；不创建独立 NTP worker。
- `HH:MM` 辉光管、日期、星期、秒数。
- 时间未同步时 fail-closed 显示等待状态。
- 每秒采样时间，冒号约 500 ms 相位动画。
- 动画/数字变化做局部刷新，不以 500 ms 周期整屏清屏。
- 完全 local-only：不使用 `HttpTransport`、`AppDataWorker` 或 `NetworkArbiter`。

## Home Assistant / 智能家居 App

### 角色

**Home Assistant 是你现有的服务器；T-Display-S3 只是 REST API 客户端。**

设备不会运行第二套 Home Assistant。运行时的数据流是：

```text
T-Display-S3
    -> GET /api/states/<entity_id>
    -> 你现有的 Home Assistant Server
```

V1 是 **只读 Dashboard**，不调用 `/api/services`，不会控制灯、门锁、空调等实体。

### 支持能力

- 配置 1–4 个 HA entity ID，可选自定义显示名称。
- 顺序读取实体，默认 30 秒刷新，可配置 30–300 秒。
- 单实体失败保留该实体最后有效状态。
- 只在 Home Assistant App 位于前台时启动新的刷新周期。
- 使用 `Authorization: Bearer <Long-Lived Access Token>`。

### HTTP / HTTPS

兼容 Home Assistant 常见的局域网默认地址：

```text
http://homeassistant.local:8123
http://192.168.x.x:8123
```

HTTP 模式无需 CA，但 **Bearer Token 在局域网上是明文传输**，只应在可信 LAN 使用。

如果你的 HA 已启用 HTTPS：

```text
https://ha.example.com
```

则必须提供 CA PEM；设备使用 `WiFiClientSecure::setCACert()` 严格校验证书，**HA 凭据路径禁止 `setInsecure()`**。

### HA 配置页

设备联网后打开：

```text
http://<T-Display-IP>:8081/
```

这里填写：

1. HA Base URL
2. Long-Lived Access Token
3. 1–4 个 entity ID
4. 可选显示名称
5. HTTPS 模式下的 CA PEM
6. 30–300 秒刷新周期

`:8081` 是 **T-Display 自己的局域网配置页**，不是 HA Server。它本身也是 HTTP，因此只在可信 LAN 使用。

配置状态接口只返回 `ha_token_set` / `ha_ca_set` 布尔值，不回显 Token 或 CA 内容；串口日志也不记录 Token。

## Crypto / 加密货币 App

- BTC / ETH / SOL 对 USDT。
- 使用 Binance 专用只读市场数据域名 `data-api.binance.vision`。
- 一次 `/api/v3/ticker/24hr` 请求取得 BTCUSDT / ETHUSDT / SOLUSDT 三个 ticker。
- 不需要 Binance API Key。
- 显示最新价 + 24h 涨跌幅。
- 60 秒刷新；仅前台主动调度。
- 响应顺序不可信，按 symbol 映射；缺失、重复、非法数值 fail-closed。
- 请求失败保留上次完整三币缓存。

## 设备信息 App

- LAN IP、SSID、RSSI、MAC。
- uptime / 当前时间。
- free heap / minimum heap / PSRAM。
- 显示现有配置页 `Web: http://<IP>/`。
- local-only，不产生外部 HTTP 请求，也不显示 Wi-Fi 密码或 Token。

## App / Worker 架构

```text
AppManager
├── MenuApp
├── StockApp
│   ├── StockController / StockScreen
│   └── MarketDataWorker
├── WeatherApp
│   └── WeatherController / OpenMeteoProvider
├── NixieClockApp
│   └── NixieClockModel / NixieClockScreen
├── HomeAssistantApp
│   └── HomeAssistantController / HomeAssistantProvider
├── CryptoApp
│   └── CryptoController / CryptoProvider
└── DeviceInfoApp

Shared low-frequency path
└── AppDataWorker
    ├── WEATHER result queue
    ├── HOME_ASSISTANT result queue
    └── CRYPTO result queue

External network execution
└── NetworkArbiter
```

Stock 保留专用 `MarketDataWorker` 以维持行情 QoS。Weather / Home Assistant / Crypto 共用 **一个** `AppDataWorker`，不为每个 App 新建 FreeRTOS worker。

结果按 `AppDataRequestType` 分队列，避免一个 App 把另一个 App 的迟到结果取走。FreeRTOS 队列传递 C++ request/result 指针，不直接 byte-copy 含 `std::string` 的非平凡对象。

所有实际外部 HTTP/TLS 操作继续经过 `NetworkArbiter`，同一时刻最多执行一个外部请求。

## 网络边界

公共行情、天气、Crypto 继续使用现有 `HttpTransport`：

```text
connect timeout: 1500 ms
HTTP/read setting: 2500 ms
TLS handshake cap: 5 s
HTTPClient reuse: false
最大保留 body: 32 KiB
```

Home Assistant 的 credentialed transport 单独处理：

- `http://` -> 普通 `WiFiClient`，明确属于可信 LAN 明文模式。
- `https://` -> `WiFiClientSecure + setCACert`，不得使用 insecure TLS。
- HA 单实体 retained body 上限 4 KiB。
- 两种 HA 模式都经过 `NetworkArbiter`。

## 配置

股票/天气公共配置仍是 **AppConfig schema v2**，NVS namespace 保持 `stockticker`。旧 v1 自动迁移时保留股票配置并补天气默认值。

Home Assistant 不改变 AppConfig schema；使用独立 `ha_config` NVS blob 存储。正常升级只写 application firmware，不擦 NVS。

## Diagnostics

```text
[md]      股票 MarketDataWorker
[appdata] WEATHER / HOME_ASSISTANT / CRYPTO
[net]     实际 HTTP/TLS 网络阶段；HA mode=HA_HTTP 或 HA_CA
[sys]     当前 App + heap/PSRAM/stack 资源
```

`[sys] app=` 可为：

```text
MENU | STOCK | WEATHER | NIXIE_CLOCK | HOME_ASSISTANT | CRYPTO | DEVICE_INFO
```

任何日志都不得输出 HA Token、Wi-Fi 密码等 secret。

## 硬件

- LILYGO T-Display-S3
- ESP32-S3
- ST7789 170×320，8-bit parallel
- 逻辑方向 320×170，rotation 3
- GPIO15：屏幕供电
- GPIO38：背光
- GPIO0 / GPIO14：按键

## 开发侧验证

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_nixie_clock_contract.py
python tools/validate_dashboard_apps_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
```

GitHub Actions 同时执行 Ubuntu native 和 Windows native，并在 ESP32-S3 build 成功后上传：

```text
tdisplay-gp-firmware-<SOURCE_SHA>
```

Artifact 包含：

```text
firmware.bin
partitions.bin
bootloader.bin
firmware-manifest.txt
```

## Codex 部署原则

Codex 只负责：

```text
exact-SHA Artifact 下载
-> manifest/SHA256 校验
-> firmware.bin 按 manifest offset 烧录
-> 串口监控
-> 真机 UI/网络/待机/稳定性测试
-> 回传证据
```

Codex 不承担常规源码修改和本地编译排错。正常升级不 erase flash/NVS，不重写 bootloader/partition table。

完整步骤见 `docs/deployment.md`，真机验收见 `docs/hardware-acceptance.md`，Provider 契约见 `docs/api-contract.md`。

## 核心原则

1. GitHub exact SHA 是事实源。
2. 默认启动 Nixie；非 Stock/Nixie App 30 秒无按键回 Nixie。
3. Stock 的行情 QoS/failover 独立保持。
4. 本地 App 不占网络 worker。
5. Weather/HA/Crypto 共用单一 AppDataWorker；结果按类型隔离。
6. 外部请求由 NetworkArbiter 串行执行。
7. Parser fail-closed，错误保留最后有效缓存。
8. HA 是客户端；HTTP 是显式可信 LAN 模式，HTTPS 必须 CA 校验。
9. 真机 PASS 只能来自实际 T-Display-S3 证据。
