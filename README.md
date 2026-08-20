# T-Display GP

LILYGO T-Display-S3 320×170 多应用桌面终端。

```text
股票 → 天气 → 辉光时钟 → 智能家居 → 加密货币 → 设备信息
```

GitHub exact SHA 是项目事实源。

## Startup / Idle

- **默认启动 NixieClock。**
- Menu / Weather / Home Assistant / Crypto / DeviceInfo：30 s 无有效按键 -> NixieClock。
- Stock / NixieClock：idle-exempt。
- 任意有效 GPIO0/GPIO14 short/long 重置 timer；网络/数据活动不重置。
- AppManager 统一管理，wrap-safe millis。

## Input

普通 App：GPIO0 short prev，GPIO14 short next，GPIO0 long menu，GPIO14 long no-op。

Menu：GPIO0/GPIO14 short 前后选，GPIO14 long enter，GPIO0 long no-op。

40 ms debounce，700 ms long，long release 不产生 short。

## Stock

Tencent quote+intraday primary，EastMoney fallback；quote/intraday health 独立；quote 优先；intraday latest-wins；有界 retry；cache-preserving；Stock 不自动 idle。

## Weather

Open-Meteo current + 3-day；默认 15 min，5–60 min；active-only；failure 保留 cache；程序化手绘水彩小猫动画。

## Nixie Clock

默认启动/idle destination；复用 ESP32 system clock / common NTP；HH:MM/date/weekday/seconds；未同步 fail-closed；1 s sample + ~500 ms colon；partial redraw；完全 local-only。

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

## Crypto

Binance market-data-only `data-api.binance.vision`；BTCUSDT / ETHUSDT / SOLUSDT；无需 API key；one request / 60 s；price + 24h change；active-only；strict symbol-mapped parser；failure 保留 last complete snapshot。

## DeviceInfo

IP、SSID/RSSI/MAC、uptime/time、heap/min heap、PSRAM、`Web: http://<IP>/`；local-only。

## Architecture

```text
Stock -> dedicated MarketDataWorker
Weather / HomeAssistant / Crypto -> exactly one shared AppDataWorker
                                   -> typed result queues
Nixie / DeviceInfo -> local-only
all actual external HTTP/TLS -> NetworkArbiter -> max one at once
```

FreeRTOS queues pass request/result pointers rather than raw-copying non-trivial std::string objects.

## Transport

Public Stock/Weather/Crypto HttpTransport: connect 1500 ms, read setting 2500 ms, TLS handshake cap 5 s, body 32 KiB, reuse false.

HA:

```text
HTTP  -> WiFiClient / trusted LAN cleartext
HTTPS -> WiFiClientSecure + CA / no insecure fallback
```

HA body max 4 KiB；both modes use NetworkArbiter。

## Config

Stock/Weather AppConfig schema v2 + `stockticker` NVS。HA 独立 `ha_config` blob。Normal app-only upgrade preserves NVS。

## Diagnostics

```text
[md] Stock
[appdata] WEATHER / HOME_ASSISTANT / CRYPTO
[net] HA mode=HA_HTTP or HA_CA
[sys] MENU/STOCK/WEATHER/NIXIE_CLOCK/HOME_ASSISTANT/CRYPTO/DEVICE_INFO
```

No secret logging。

## Verification

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_nixie_clock_contract.py
python tools/validate_dashboard_apps_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
```

CI also runs Windows native and publishes exact-SHA firmware artifact with manifest/hashes。

## Codex

Codex only verifies/downloads exact artifact, flashes manifest app offset, monitors 115200 serial, performs physical UI/network/idle/soak tests and returns evidence。Normal deployment does not rebuild, edit source, erase NVS or rewrite partitions/bootloader。

See `docs/deployment.md`, `docs/api-contract.md`, `docs/hardware-acceptance.md`。
