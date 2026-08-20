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

GitHub Actions 还必须完成 Windows native 测试，并上传 `tdisplay-gp-firmware-<SOURCE_SHA>`，包含 `firmware.bin`、`partitions.bin`、`bootloader.bin`、`firmware-manifest.txt`。

Manifest 必须记录 exact `source_sha`、环境、application offset 和二进制 SHA256。

## 2. Codex 下载与校验

Codex 只使用最终批准 SHA 对应的 verified Artifact。下载后必须满足：

```text
manifest source_sha == 批准 SHA
actual firmware.bin SHA256 == manifest firmware_sha256
```

任一不一致：禁止烧录。

## 3. 烧录边界

当前 partition layout 未改变。正常升级只把 `firmware.bin` 写到 manifest application offset（当前通常 `0x10000`）。

禁止默认执行：erase_flash、擦 NVS、重写 bootloader.bin、重写 partitions.bin。这样保留 Wi-Fi、股票/天气和 Home Assistant 配置。

串口：115200 baud。

## 4. 启动预期

正常启动出现：

```text
[boot] multi-app loop ready
```

**默认前台为辉光时钟。**

菜单：

```text
股票 → 天气 → 辉光时钟 → 智能家居 → 加密货币 → 设备信息
```

## 5. 系统待机规则

```text
Menu             30s 无按键 -> NixieClock
Weather          30s 无按键 -> NixieClock
Home Assistant   30s 无按键 -> NixieClock
Crypto           30s 无按键 -> NixieClock
DeviceInfo       30s 无按键 -> NixieClock
Stock            >30s       -> 仍保持 Stock
NixieClock       >30s       -> 仍保持 NixieClock
```

任意有效 GPIO0/GPIO14 短按或长按重新开始 30 秒计时；网络刷新/数据返回不重置。

## 6. 操作

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

40 ms debounce，700 ms long，长按释放不能再产生 short。

## 7. 股票/天气配置

现有 `http://<device-ip>/` / Captive Portal 管理 Wi-Fi、3–5 只 A 股、股票刷新、Weather enable、地点/经纬度/天气刷新。

AppConfig 仍 schema v2；升级不擦 NVS。

## 8. Home Assistant 配置

### 角色

**Home Assistant 是用户已经部署好的服务器；T-Display-S3 只是 REST API 客户端。** 板子不会运行第二套 Home Assistant。

设备联网后打开：

```text
http://<device-ip>:8081/
```

该页面只是 T-Display 的 HA 客户端配置页，不是 HA Server。

填写：

1. 已有 HA Server Base URL
2. Long-Lived Access Token
3. 1–4 个 entity ID
4. 可选 label
5. 30–300 秒刷新
6. 仅 HTTPS 模式需要 CA PEM

### HTTP 模式

兼容常见已有 LAN HA：

```text
http://homeassistant.local:8123
http://192.168.x.x:8123
```

无需 CA。Bearer Token 在 LAN 明文传输，仅在可信 LAN 使用。

### HTTPS 模式

```text
https://ha.example.com
```

必须配置 CA PEM。固件使用 `setCACert()` 严格验证，不允许 HA HTTPS 使用 `setInsecure()`。

HA status 只暴露 `ha_token_set` / `ha_ca_set`，不得回显 Token/CA。

## 9. Crypto

Crypto 使用 Binance 只读市场数据域 `data-api.binance.vision`，不需要 API key。前台约每 60 秒一次请求 BTCUSDT / ETHUSDT / SOLUSDT 24h ticker。

验证价格、24h 涨跌、离开后停止新周期、失败保留缓存。

## 10. 串口日志

```text
[md]      Stock
[appdata] WEATHER / HOME_ASSISTANT / CRYPTO
[net]     transport timing
[sys]     active app / heap / PSRAM / stack
```

HA 网络模式：`mode=HA_HTTP` 或 `mode=HA_CA`。任何日志不得出现 HA Token。

`[sys] app=`：

```text
MENU | STOCK | WEATHER | NIXIE_CLOCK | HOME_ASSISTANT | CRYPTO | DEVICE_INFO
```

## 11. Smoke test

烧录后至少确认：

1. 320×170 landscape 正确。
2. 开机默认 NixieClock。
3. Nixie 时间同步/等待状态、辉光、冒号动画正常，无整屏闪烁。
4. 菜单进入六个 App。
5. 30 秒 idle + Stock/Nixie 例外正确。
6. DeviceInfo 真实 IP/SSID/RSSI/MAC/heap。
7. Weather 实时数据/三日预报正常。
8. HA 作为客户端连接用户已有服务器并读取实体。
9. Crypto BTC/ETH/SOL 正常。
10. Stock 缓存、切股、Tencent/EastMoney 行为不回归。
11. 无 watchdog/panic/unexpected reboot。

## 12. Stock Provider 回归

保持：

```text
quote: Tencent -> threshold -> EastMoney -> Tencent probes -> recover
intraday: Tencent -> bounded retries -> EastMoney fallback
```

正常优先 `Q:TX` / `I:TX`。Tencent 分时最终失败应看到 `fallback=TX->EM`；Intraday 健康不得污染 Quote health。

## 13. 稳定性

至少 100 次全 App/menu 循环：watchdog=0、unexpected reboot=0、freeze=0、short-after-long=0、明显 heap leak=0、错误后台重绘=0、Nixie 周期整屏闪烁=0。

最终分别记录：

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
