# T-Display GP 部署与烧录指南

本项目固定采用：**Web ChatGPT 开发/编译，Codex 只烧录/真机测试**。

## 1. 开发侧门禁

最终 source SHA 必须通过：

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_nixie_clock_contract.py
python tools/validate_dashboard_apps_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
```

GitHub Actions 还必须完成 Windows native 测试，并上传：

```text
tdisplay-gp-firmware-<SOURCE_SHA>
```

Artifact：

```text
firmware.bin
partitions.bin
bootloader.bin
firmware-manifest.txt
```

Manifest 必须记录 exact `source_sha`、环境、application offset 和三个二进制 SHA256。

## 2. Codex 下载与校验

Codex 只使用最终批准 SHA 对应的 verified Artifact。下载后至少核对：

```text
manifest source_sha == 批准 SHA
actual firmware.bin SHA256 == manifest firmware_sha256
```

任一不一致立即停止，不烧录。

## 3. 烧录边界

当前 partition layout 未改变。正常升级只把 `firmware.bin` 写到 manifest 的 application offset（当前通常为 `0x10000`）。

禁止默认执行：

```text
erase_flash
擦除 NVS
重写 bootloader.bin
重写 partitions.bin
```

这样保留 Wi-Fi、股票/天气和 Home Assistant 配置。

串口监控：115200 baud。

## 4. 启动预期

正常启动：

```text
[boot] multi-app loop ready
```

**默认前台应为辉光时钟，不再是 StockApp。**

当前菜单：

```text
股票 → 天气 → 辉光时钟 → 智能家居 → 加密货币 → 设备信息
```

## 5. 系统待机规则

Codex 必须验证：

```text
Menu             30s 无按键 -> NixieClock
Weather          30s 无按键 -> NixieClock
Home Assistant   30s 无按键 -> NixieClock
Crypto           30s 无按键 -> NixieClock
DeviceInfo       30s 无按键 -> NixieClock
Stock            >30s       -> 仍保持 Stock
NixieClock       >30s       -> 仍保持 NixieClock
```

任意有效 GPIO0/GPIO14 短按或长按应重新开始 30 秒计时。网络刷新/数据返回不得重置计时器。

## 6. 普通 App / 菜单操作

普通 App：

```text
GPIO0 short  -> previous
GPIO14 short -> next
GPIO0 long   -> menu
GPIO14 long  -> reserved/no-op
```

菜单：

```text
GPIO0 short  -> previous app
GPIO14 short -> next app
GPIO0 long   -> no-op
GPIO14 long  -> enter selected app
```

长按阈值约 700 ms，释放时不能再产生 short。

## 7. 股票/天气配置

现有公共配置仍由 Captive Portal / `http://<device-ip>/` 管理：

- Wi-Fi
- 3–5 只 A 股
- 3/4/5 秒股票刷新
- Weather enable
- 地点名称、经纬度、5–60 分钟天气刷新

AppConfig schema 仍为 v2；正常升级不擦 NVS。

## 8. Home Assistant 配置

### 角色

**Home Assistant 是用户已有服务器；LILYGO 是 REST 客户端。**

设备联网后，浏览器打开：

```text
http://<device-ip>:8081/
```

该页面只是 T-Display 的 HA 配置页，不是 HA Server。

填写：

1. HA Base URL
2. Long-Lived Access Token
3. 1–4 个 entity ID
4. 可选显示 label
5. 刷新 30–300 秒
6. 仅 HTTPS 模式需要 CA PEM

### HTTP 模式

兼容现有 HA 默认 LAN 部署：

```text
http://homeassistant.local:8123
http://192.168.x.x:8123
```

无需 CA。注意 Bearer Token 会在 LAN 明文传输，仅在可信 LAN 使用。

### HTTPS 模式

```text
https://ha.example.com
```

必须配置 CA PEM。固件使用 `setCACert()` 严格验证，不允许 HA HTTPS 路径使用 `setInsecure()`。

HA 配置状态只应暴露 `ha_token_set` / `ha_ca_set`，不得回显 Token/CA。

## 9. Crypto

Crypto 使用 Binance 只读市场数据域 `data-api.binance.vision`，不需要 API key。前台约每 60 秒一次请求 BTCUSDT/ETHUSDT/SOLUSDT 三个 24h ticker。

应验证：

- BTC / ETH / SOL 三行都有价格。
- 24h 涨跌显示正常。
- 离开 Crypto 后不继续启动新 Crypto 请求。
- 网络失败后保留最后有效三币缓存。

## 10. 串口日志

重点：

```text
[md]      Stock
[appdata] WEATHER / HOME_ASSISTANT / CRYPTO
[net]     transport timing
[sys]     active app / heap / PSRAM / stack
```

HA 网络模式：

```text
mode=HA_HTTP
mode=HA_CA
```

任何日志中都不应出现 HA Token。

`[sys] app=` 可为：

```text
MENU
STOCK
WEATHER
NIXIE_CLOCK
HOME_ASSISTANT
CRYPTO
DEVICE_INFO
```

## 11. Smoke test

烧录后至少执行：

1. 320×170 landscape 正确。
2. 开机默认 NixieClock。
3. 时间未同步时等待状态；同步后显示 HH:MM、日期、星期、秒数。
4. Nixie 500ms 冒号动画无周期性整屏黑闪。
5. 菜单能进入六个 App。
6. 逐项验证 30 秒 idle 规则与 Stock 例外。
7. DeviceInfo 显示真实 IP/SSID/RSSI/MAC/heap。
8. Weather 实时数据和三日预报正常。
9. HA 连接用户已有服务器并读取配置实体。
10. Crypto BTC/ETH/SOL 数据正常。
11. 返回 Stock 后缓存可见，按钮切股和 Tencent/EastMoney 策略不回归。
12. 串口无 watchdog / panic / Guru Meditation / unexpected reboot。

## 12. Stock 网络检查

保持既有策略：

```text
quote: Tencent -> threshold -> EastMoney -> Tencent probes -> recover
intraday: Tencent -> bounded retries -> EastMoney fallback
```

正常应优先 `Q:TX` / `I:TX`。腾讯分时最终失败时应有 `fallback=TX->EM`；Intraday 成败不得改变 quote provider health。

## 13. 稳定性

至少完成 100 次跨 App/menu 循环，确认：

```text
watchdog = 0
unexpected reboot = 0
freeze = 0
short-after-long = 0
明显持续 heap 泄漏 = 0
错误 App 重绘 = 0
Nixie 周期性整屏闪烁 = 0
```

最终状态应分别记录：

```text
VALIDATORS PASS
UBUNTU NATIVE PASS
WINDOWS NATIVE PASS
ESP32 BUILD PASS
VERIFIED ARTIFACT PASS
FLASH PASS
NIXIE STARTUP/IDLE PASS
HOME ASSISTANT CLIENT PASS
CRYPTO LIVE PASS
WEATHER PASS
STOCK PASS
FULL HARDWARE ACCEPTANCE PASS
```

详细物理验收见 `docs/hardware-acceptance.md`。
