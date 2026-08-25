# T-Display GP

LILYGO T-Display-S3 320×170 多应用桌面终端。

```text
股票 → 天气 → 辉光时钟 → 智能家居 → 加密货币 → 设备信息
```

GitHub exact SHA 是项目事实源。

## Startup / Navigation

- **默认启动 NixieClock。**
- **没有自动 idle 跳转。** Menu / Stock / Weather / NixieClock / Home Assistant / Crypto / DeviceInfo 都会保持当前页面，直到用户主动按键切换。
- `AppManager` 不维护无操作计时器，也不会因为网络/数据活动自动切 App。

## Home Assistant

**用户已有 Home Assistant 是服务器；T-Display-S3 只是只读 REST API 客户端。LILYGO 不运行第二套 HA。**

```text
T-Display -> GET <existing HA>/api/states/<entity_id>
```

- V1 read-only，不调用 `/api/services`。
- 1–4 entities，optional labels。
- refresh 30–300 s，default 30 s，active-only。
- Bearer Long-Lived Access Token。
- per-entity last-valid cache。

HTTP existing LAN server：

```text
http://homeassistant.local:8123
http://<ha-ip>:8123
```

无需 CA，但 Token 在 LAN 明文传输，仅 trusted LAN。

HTTPS：必须 CA PEM；`WiFiClientSecure::setCACert()`；**禁止 HA HTTPS `setInsecure()`**。

T-Display HA client config：

```text
http://<T-Display-IP>:8081/
```

`:8081` 是板子的配置页，不是 HA Server。Status 只暴露 `ha_token_set` / `ha_ca_set`，不返回 Token/CA；serial 不记录 Token。

## Other Apps

### Stock
Tencent quote+intraday primary，EastMoney fallback；quote/intraday health 独立；quote 优先；intraday latest-wins；有界 retry；cache-preserving。

### Weather
Open-Meteo 仍获取 current + 3-day structured data；默认 15 min，5–60 min；active-only；failure 保留 cache。

天气屏重新排版为：

```text
left 0..149: current weather + compact Today/Tomorrow
right 152..319, y=27..152: Bad Apple!! 168×126 monochrome player
```

- UI 只显示“今 / 明”，不显示“后天”；Provider 内部 three-day 数据契约不变。
- Bad Apple：**168×126、2190 frames、10 FPS、约 219 s、静音、循环**。
- 1-bit packed first frame + XOR sparse-delta compressed asset；当前生成资产约 1.72 MB。
- 进入 Weather 后从 frame 0 播放；离开 Weather 后停止视频刷新；只重绘视频区域，不做 10 FPS 整屏清屏。
- 不新增 FreeRTOS task，不占用新的 AppDataWorker/NetworkArbiter 路径。
- 构建时 `tools/prepare_bad_apple_asset.py` 从锁定 source commit/blob 获取转换输入、校验 Git blob SHA1、经 ffmpeg 缩放/抽帧/二值化，并对全部 delta 做 round-trip 校验；原始 MP4 和生成 C++ 资产均不提交仓库。
- 仓库许可证只覆盖本项目代码；**不对 Bad Apple!! 原始 PV/音乐重新授权**，相关权利归各自权利人。

### Nixie Clock
默认启动；复用 ESP32 system clock/common NTP；HH:MM/date/weekday/seconds；未同步 fail-closed；1 s sample + ~500 ms colon；partial redraw；local-only。

### Crypto
Binance market-data-only `data-api.binance.vision`；BTCUSDT/ETHUSDT/SOLUSDT；无需 API key；one request / 60 s；price + 24h change；active-only；strict symbol-mapped parser；failure 保留 last complete snapshot。

### DeviceInfo
IP、SSID/RSSI/MAC、uptime/time、heap/min heap、PSRAM、`Web: http://<IP>/`；local-only。

## Input

```text
normal app: GPIO0 short prev; GPIO14 short next; GPIO0 long menu; GPIO14 long no-op
menu:       GPIO0 short prev; GPIO14 short next; GPIO0 long no-op; GPIO14 long enter
```

40 ms debounce，700 ms long，long release 不产生 short。

## Architecture

```text
Stock -> dedicated MarketDataWorker
Weather / HomeAssistant / Crypto -> exactly one shared AppDataWorker -> typed result queues
Nixie / DeviceInfo -> local-only
all actual external HTTP/TLS -> NetworkArbiter -> max one at once
Weather Bad Apple playback -> local flash asset only; no runtime network/task
```

FreeRTOS queues pass request/result pointers rather than raw-copying non-trivial std::string objects。

## Transport / Config

Public Stock/Weather/Crypto HttpTransport: connect 1500 ms，read setting 2500 ms，TLS handshake cap 5 s，body max 32 KiB，reuse false。

HA: HTTP uses WiFiClient trusted-LAN cleartext; HTTPS uses WiFiClientSecure + CA with no insecure fallback; body max 4 KiB; both acquire NetworkArbiter。

Stock/Weather AppConfig schema v2 + `stockticker` NVS。HA independent `ha_config` blob。Normal app-only upgrade preserves NVS。

## Diagnostics

```text
[md] Stock
[appdata] WEATHER / HOME_ASSISTANT / CRYPTO
[net] HA mode=HA_HTTP or HA_CA
[sys] MENU/STOCK/WEATHER/NIXIE_CLOCK/HOME_ASSISTANT/CRYPTO/DEVICE_INFO
```

No secret logging。

## Verification

Host/CI needs ffmpeg for the generated Bad Apple firmware asset.

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_nixie_clock_contract.py
python tools/validate_dashboard_apps_contract.py
python tools/validate_bad_apple_contract.py
pio test -e native
python tools/prepare_bad_apple_asset.py
pio run -e lilygo-t-display-s3
```

CI also runs Windows native and publishes exact-SHA artifact with manifest/hashes。

Codex only flashes/verifies/tests the prebuilt exact-SHA application artifact; no normal local rebuild/source edit/NVS erase/partition rewrite。

Details: `docs/deployment.md`, `docs/api-contract.md`, `docs/hardware-acceptance.md`。
