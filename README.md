# T-Display GP

基于 **LILYGO T-Display-S3** 的可扩展桌面信息终端。当前包含 **A 股行情、天气、设备信息** 三个 App，并通过统一主菜单切换；空气质量、Home Assistant、服务监控可在后续按同一架构继续增加。

> **GitHub `dgcary/t-display-gp` 是项目唯一事实源。** 开发、修复和 Codex 部署都应从 GitHub 指定分支/提交开始。

## 当前功能

### 多 App Shell

- 默认开机进入股票 App，保持原使用习惯
- GPIO0 短按：当前 App 上一项；GPIO14 短按：当前 App 下一项
- **GPIO0 长按 700 ms：返回主菜单**
- 主菜单 GPIO0/GPIO14 短按：前后选择 App
- **主菜单 GPIO14 长按 700 ms：进入选中 App**
- 长按只触发一次，不在释放时再误触发短按
- 非前台 App 不绘屏、不主动高频调度，但保留最后有效缓存

### 股票 App

- 沪/深/北 A 股，股票池 3–5 只
- 报价 3–5 秒刷新可配置
- **腾讯：报价 + 分时主源**
- **东方财富：报价 + 分时备用源**
- 320×170 横屏
- 网络/Provider 失败保留最后有效报价和分时图
- 分时午休断点、昨收/今开参考线
- Quote/Intraday 健康状态与 Provider failover 独立
- 每个新分时周期先尝试腾讯；腾讯有限重试仍失败后自动切东方财富分时接口
- 完整分时周期失败后等待正常刷新间隔，不产生 empty-cache retry storm
- 页脚区分 `Q:` 报价源和 `I:` 分时源，例如常态 `Q:TX I:TX`
- StockApp 退出到菜单后暂停新的行情 Worker 执行；已经开始的 HTTPS 请求允许自然完成，返回股票时恢复原 QoS/刷新

### 天气 App

- Open-Meteo Provider，独立于 UI/Controller
- 地点名称 + 经纬度由 Web/Captive Portal 配置
- 当前温度、体感、湿度、风速、降雨概率、天气状态
- 今天/明天/后天高低温与中文天气状态
- 重点信息按天气/温度着色
- 右侧天气角色采用 **手绘水彩风奶油/暖棕小猫**，用多层低饱和“水彩晕染”绘制，不依赖 GIF/大位图
- 晴/多云/雾/雨/雪/雷雨背景，以及高温汗滴、雨伞、低温围巾、睡眠符号、雷暴闪电等反应
- 两帧动画约 500 ms 切换；动画只局部刷新右侧宠物区域，避免整屏闪烁
- 默认 15 分钟刷新，可配置 5–60 分钟
- 请求失败保留最后有效天气缓存
- WeatherApp 不在前台时不主动刷新
- 天气错误不会污染股票状态

### 设备信息 App

- 当前 LAN IP（大号显示）
- SSID / RSSI / MAC
- uptime / 当前时间
- free heap / minimum heap / PSRAM
- 直接提示 `Web: http://<IP>/`，方便手机或电脑打开现有 Web 配置页
- 本地只读，不发起外部 HTTP 请求，也不显示 Wi-Fi 密码/API secret

## 主菜单

```text
股票 → 天气 → 设备信息
```

启动默认仍进入股票。任意普通 App 中长按 GPIO0 返回主菜单；主菜单短按左右选择，长按 GPIO14 进入。

## 架构

```text
AppManager
├── MenuApp
├── StockApp
│   ├── StockController
│   ├── StockScreen
│   └── MarketDataWorker
├── WeatherApp
│   ├── WeatherController
│   ├── WeatherScreen / WeatherVisuals / WeatherCatArt
│   └── AppDataWorker / OpenMeteoProvider
└── DeviceInfoApp
    └── DeviceInfoScreen

Shared
├── DeviceLayer / ButtonInput
├── Provisioning / ConfigStore
├── NetworkArbiter
└── HttpTransport
```

`MarketDataWorker` 保留股票专用 QoS/重试；低频天气以及未来空气质量/Home Assistant/服务监控统一走共享 App-data 路径，不为每个 App 创建一个 FreeRTOS Worker。DeviceInfo 只读取本地状态，不占用 AppDataWorker/NetworkArbiter。

## 网络与内存边界

所有外部 HTTPS 请求经过 `NetworkArbiter` 串行执行：**同一时刻最多一个外部 HTTP/TLS 请求**。这避免股票与天气同时 TLS handshake 时产生不必要的运行时 Heap 峰值。

现有 transport 约束保持：

```text
TCP/connect: 1500 ms
TLS handshake: 5 s
HTTP/read setting: 2500 ms
HTTPClient reuse: false
最大保留响应: 32 KiB
```

串口除股票 `[md]` 外，天气请求使用 `[appdata]`，并每 60 秒输出一次运行时资源：

```text
[sys] app=STOCK|MENU|WEATHER|DEVICE_INFO heap_free=... heap_min=... psram_free=... psram_total=... main_stack_hwm=...
```

真机长时间切换 App 时应关注 `heap_min` 和 `heap_free` 是否持续单向下降。

## 股票稳定性设计

股票 HTTP 始终在 `MarketDataWorker` 中执行，主循环不做阻塞网络请求。

请求优先级：

```text
当前股票报价 > 后台报价 > 主源恢复探测 > 分时 > 分时重试
```

分时采用 latest-wins。腾讯可恢复网络/服务器错误最多 3 次尝试，约在 1.5 秒、4 秒后延迟重试，且重试必须让出报价请求；腾讯分时周期最终无法完成时才交给东方财富。东方财富作为二级分时源失败后，该周期结束，不递归 fallback。

报价默认同样使用腾讯。连续 3 次腾讯主报价失败进入东方财富备用，备用期间按 120 秒节奏探测腾讯；连续 2 次腾讯探测成功恢复主源。

## 配置 schema

配置为 schema v2，仍使用已有 `stockticker` NVS namespace。

已有 schema v1 设备升级时：

- 原 3–5 只股票保留
- 股票显示名保留
- 3/4/5 秒刷新周期保留
- 自动补充天气默认配置（默认关闭、15 分钟）
- 成功读取后 best-effort 写回 v2 格式

Weather 启用后必须配置：

```text
地点名称
纬度 -90..90
经度 -180..180
刷新周期 5..60 分钟
```

## 首次使用 / Web 设置

无有效配置时连接 `TDisplay-GP-Setup`。Captive Portal 与局域网 Web 设置页支持：

1. Wi-Fi
2. 3–5 个 A 股代码和可选显示名
3. 3/4/5 秒股票刷新周期
4. 天气启用状态
5. 地点名称、纬度、经度
6. 5–60 分钟天气刷新周期

已经联网时，如忘记设备 IP：进入 **设备信息**，直接查看 `Web: http://<IP>/`。

保存配置后采用受控重启，避免运行中部分模块使用新旧配置混合状态。

## 硬件

- LILYGO T-Display-S3
- ESP32-S3
- ST7789 170×320，8-bit parallel
- 应用逻辑：320×170 landscape，rotation 3
- GPIO15：屏幕供电
- GPIO38：背光
- GPIO0：上一项 / 长按返回菜单
- GPIO14：下一项 / 菜单长按进入
- TFT RGB 顺序：`TFT_RGB`
- `INIT_SEQUENCE_3`

## 开发与烧录

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
pio run -e lilygo-t-display-s3 -t upload
pio device monitor -b 115200
```

GitHub Actions 同时运行 Ubuntu native + Windows native。任一测试、合同校验或 firmware build 失败时不得烧录。

完整步骤见 [docs/deployment.md](docs/deployment.md)。

## 目录

```text
AGENTS.md              Codex / 自动化 Agent 规则
include/               固件常量
lib/core/              配置、股票代码、交易时钟等纯逻辑
lib/providers/         行情 Provider 接口/解析器
src/app/               AppShell / StockApp / WeatherApp / DeviceInfoApp / Controller
src/network/           HTTP、网络互斥、行情/通用 Worker、Provider、配网
src/device/            T-Display-S3 硬件与输入层
src/ui/                Menu / Stock / Weather / DeviceInfo UI
test/                  PlatformIO native tests
tools/                 TFT / 配网 / HTTP contract validators
docs/                  API、部署、真机验收、设计规格/计划
```

## 核心原则

1. `main.cpp` 只负责公共启动与 AppManager 驱动，不承载具体 App 业务。
2. App 缓存优先：退出/网络失败不清最后有效数据。
3. 非前台 App 不获得绘屏权。
4. 不为每个新 App 创建独立 Worker；低频功能复用 App-data 路径。
5. 外部 TLS 串行执行，避免并发 TLS 内存峰值。
6. Quote/Intraday Provider 健康独立，分时 fallback 不能污染报价 failover。
7. Parser fail-closed，不通过放宽字段/无限重试掩盖 Provider 问题。
8. 真机 PASS 必须来自实体 T-Display-S3，host test/firmware build 不能代替。

## Provider 与安全说明

股票 Provider 约定和天气 Provider 字段见 [docs/api-contract.md](docs/api-contract.md)。

当前固件仍延续既有 `WiFiClientSecure::setInsecure()` 行为；多 App 改造没有进一步降低 TLS 安全性。若未来 Home Assistant 接入长期访问 Token，必须单独设计凭据存储和严格 TLS 验证，不能直接照搬当前公开数据源的安全边界。

## 真机验收

除了原股票稳定性，还必须验证：

- 开机默认 StockApp
- 长按 GPIO0 返回菜单
- 菜单可进入 Stock / Weather / DeviceInfo
- DeviceInfo 显示真实 IP 且 Web 配置页可访问
- Weather 能获取实时数据，中文三日预报正常
- 手绘水彩小猫动画/天气反应正常且无整屏闪烁
- 常态行情优先看到 `Q:TX I:TX`
- Tencent 分时最终失败时能出现 `fallback=TX->EM`，东方财富成功后 footer `I:EM`
- Quote 继续独立正常更新
- Weather/Stock 失败均保留各自缓存
- Stock ⇄ Menu ⇄ Weather ⇄ DeviceInfo 至少 100 次无 watchdog/reboot/freeze
- `[sys]` Heap 无持续单向下降

详见 [docs/hardware-acceptance.md](docs/hardware-acceptance.md)。
