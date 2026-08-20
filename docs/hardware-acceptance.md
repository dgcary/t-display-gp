# T-Display-S3 Hardware Acceptance

只用于 **LILYGO T-Display-S3 真机**。Host test / firmware build 不能代替实体板验收。

每次记录：branch、完整 source SHA、Actions run、artifact、firmware SHA256、日期、Wi-Fi、串口和测试结果。

## 1. Artifact / flash

开发侧必须已通过：

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_nixie_clock_contract.py
python tools/validate_dashboard_apps_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
```

Codex 下载 exact-SHA `tdisplay-gp-firmware-<SOURCE_SHA>`，核对：

```text
manifest source_sha == approved SHA
actual firmware.bin SHA256 == manifest firmware_sha256
```

正常升级仅写 `firmware.bin` 到 manifest `firmware_offset`（当前通常 `0x10000`）。禁止默认 erase flash/NVS、重写 bootloader/partition table。

串口 115200。启动必须出现：

```text
[boot] multi-app loop ready
```

无 panic/watchdog/Guru Meditation/启动循环。

## 2. 硬件/UI 基线

- ST7789 physical 170×320；logical **320×170 landscape rotation 3**。
- GPIO15 screen power HIGH。
- GPIO38 backlight。
- GPIO0/GPIO14 active-low pull-up。
- 中文清晰，无裁切/花屏/跨 App 残留。

## 3. 菜单与按键

菜单必须包含：

```text
股票
天气
辉光时钟
智能家居
加密货币
设备信息
```

普通 App：GPIO0 short prev、GPIO14 short next、GPIO0 long menu、GPIO14 long no-op。

Menu：GPIO0/GPIO14 short 前后选择，GPIO14 long enter，GPIO0 long no-op。

验收：40 ms debounce、700 ms long、long release 不额外 short、无 hold auto-repeat。

## 4. 默认启动与 30 秒待机

### 默认启动

重启后必须直接进入 **辉光时钟**，不是 StockApp/Menu。

### 自动待机

分别进入下列页面，不按任何键：

```text
Menu             30s -> NixieClock
Weather          30s -> NixieClock
Home Assistant   30s -> NixieClock
Crypto           30s -> NixieClock
DeviceInfo       30s -> NixieClock
```

允许主循环调度误差，但不应明显早于 30 秒，也不应多拖数秒。

### 按键重置

在上述任一页面停留约 20 秒，产生一个有效 GPIO0/GPIO14 short/long 事件，再验证自动切换约从该事件重新计 30 秒。

### 豁免

```text
Stock       连续停留 >60s -> 仍为 Stock
NixieClock  连续停留 >60s -> 仍为 NixieClock
```

网络请求完成、天气动画、HA/Crypto 数据刷新不能重置 idle timer。

## 5. NixieClock

时间未同步时必须显示明确等待状态，不能显示伪有效 1970/00:00。

同步后验证：

- HH:MM、日期、星期、秒数。
- 暖橙/琥珀辉光管视觉。
- 冒号约 500 ms 相位动画。
- 每分钟变化无整屏黑闪。
- 冒号动画不每 500 ms 整屏清屏。
- 页面停留不产生由 Nixie 触发的 `[md]` / `[appdata]`。
- `[sys] app=NIXIE_CLOCK`。

## 6. DeviceInfo

至少显示：LAN IP、SSID、RSSI、MAC、uptime、local time、heap/min heap、PSRAM、`Web: http://<IP>/`。

同 LAN 浏览器应能访问 `http://<IP>/`。页面不得显示 Wi-Fi password、HA Token 等 secret。

## 7. Weather

配置有效地点后：

- 当前温度/体感/湿度/风速/降雨概率。
- 中文天气状态。
- 今/明/后三日高低温和中文状态。
- 手绘水彩风小猫两帧动画稳定，无整屏闪烁。
- `[appdata] type=WEATHER` 可观察。

获得有效缓存后制造安全离线：旧缓存保留，无 tight retry；恢复网络后正常恢复。Weather 错误不得改变 Stock provider health。

## 8. Home Assistant

### 前提

使用用户**已有 Home Assistant Server**。T-Display 只是 REST client。

设备配置页：

```text
http://<device-ip>:8081/
```

配置：1–4 entity IDs、可选 labels、Long-Lived Access Token、30–300s refresh、Base URL；HTTPS 才需要 CA PEM。

`:8081` 不是 HA Server。

### HTTP 模式

若现有服务器是默认 LAN HTTP，例如：

```text
http://homeassistant.local:8123
http://192.168.x.x:8123
```

验证：

- 不要求 CA。
- 能读取配置实体。
- UI 显示 state/unit/friendly label。
- `[net] ... mode=HA_HTTP ...`。
- `[appdata] ... type=HOME_ASSISTANT entity=...`。

HTTP Bearer Token 在 LAN 明文传输，只在可信 LAN 测试。

### HTTPS 模式（若环境具备）

配置 HTTPS Base URL + 正确 CA PEM：

- 能读取实体。
- `[net] ... mode=HA_CA ...`。

错误/缺失 CA 应拒绝配置或连接失败，不能 insecure fallback。

### Secret 验收

检查：

- `http://<device-ip>:8081/api/ha/status` 不含 Token/CA 内容，只允许 `ha_token_set` / `ha_ca_set`。
- 串口 `[ha]` / `[net]` / `[appdata]` 不出现 Token 或完整 Authorization header。

### 缓存

先成功读取实体，再断开 HA/网络：每个已有实体保持最后有效状态并显示错误/陈旧状态，不清空其他实体。

V1 不验收灯控/门锁/服务调用，因为当前版本明确 read-only。

## 9. Crypto

进入“加密货币”：

- BTC / ETH / SOL 三行。
- USDT 最新价。
- 24h 涨跌幅。
- 数据来源为 Binance Spot market-data-only。
- `[appdata] type=CRYPTO`。
- 对应 `[net] host=data-api.binance.vision ...`。

停留超过 60 秒应能看到下一次正常刷新；不要出现秒级 tight loop。

成功后制造安全离线：最后完整三币 snapshot 保留。退出 Crypto 后不得持续启动新 Crypto 周期。

## 10. Stock 回归

Stock 不受 30 秒 idle 影响。

必须保留：

- GPIO0/GPIO14 一次只切一只。
- 正涨红、下跌绿。
- 中文名称、当前价、开高低昨、量额。
- 分时图、昨收/今开参考、午休断点。
- Quote/Intraday 健康独立。
- 正常优先 Tencent。
- 新 intraday cycle 先 Tencent。

正常有缓存时常见：

```text
Q:TX I:TX
```

腾讯分时最终失败且 EastMoney 健康时：

```text
[md] ... fallback=TX->EM
Q:TX I:EM
```

Quote failover 仍按 3 Tencent failure -> EastMoney；恢复需 2 Tencent probe successes。

## 11. Shared network / late result

快速在 Weather / HA / Crypto / Menu / Nixie / Stock 间切换：

- 同时最多一个实际外部 HTTP/TLS operation。
- Weather/HA/Crypto 共用一个 AppDataWorker，但结果不得串 App。
- inactive app 的 late completion 不得覆盖当前 TFT。
- 30 秒 idle 切换发生后，旧 remote app 不应在边界再启动额外一次请求。
- Nixie/DeviceInfo 不占 AppDataWorker/NetworkArbiter。

## 12. 资源稳定性

收集约每 60 秒 `[sys]`：

```text
[sys] app=MENU|STOCK|WEATHER|NIXIE_CLOCK|HOME_ASSISTANT|CRYPTO|DEVICE_INFO heap_free=... heap_min=... psram_free=... psram_total=... main_stack_hwm=...
```

至少记录：开机、Stock 10 min、Weather 成功后、HA 成功后、Crypto 两轮刷新后、Nixie 5 min、100 次切换后、Wi-Fi 中断恢复后。

通过：无持续单向 heap_free 下降、stack HWM 不逼近 0、无 allocation failure/panic/watchdog。

## 13. 100 次切换 soak

循环覆盖全部 App，例如：

```text
Nixie -> Menu -> Stock -> Menu -> Weather -> Menu -> HA -> Menu -> Crypto -> Menu -> DeviceInfo -> Menu -> Nixie
```

至少 100 次 App/menu transition：

```text
watchdog = 0
unexpected reboot = 0
freeze = 0
short-after-long = 0
明显 heap leak = 0
background wrong-screen redraw = 0
```

## 14. 最终报告格式

```text
SOURCE SHA:
ACTIONS RUN / ARTIFACT:
PORT:
FIRMWARE SHA256:

FLASH: PASS/FAIL
DISPLAY/INPUT: PASS/FAIL
NIXIE DEFAULT START: PASS/FAIL
30S IDLE POLICY: PASS/FAIL
STOCK IDLE EXEMPT: PASS/FAIL
NIXIE IDLE EXEMPT: PASS/FAIL
WEATHER: PASS/FAIL
HOME ASSISTANT HTTP: PASS/FAIL/NOT TESTED
HOME ASSISTANT HTTPS CA: PASS/FAIL/NOT TESTED
HA SECRET LEAK CHECK: PASS/FAIL
CRYPTO: PASS/FAIL
STOCK/TENCENT: PASS/FAIL
TX->EM FALLBACK: PASS/FAIL/NOT TRIGGERED
100-TRANSITION SOAK: PASS/FAIL
HEAP/STABILITY: PASS/FAIL
FULL HARDWARE ACCEPTANCE: PASS/FAIL/PARTIAL
```

任何 FAIL 都附串口原始片段、复现步骤、时间点和照片/截图（适用时）。
