# T-Display-S3 Hardware Acceptance

只用于 **LILYGO T-Display-S3 真机**。Host test / firmware build 不能代替实体板验收。

每次记录：branch、完整 source SHA、Actions run、artifact、firmware SHA256、日期、Wi-Fi、串口和结果。

## Artifact / Flash

开发侧必须已通过：

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

Bad Apple 构建阶段需要 ffmpeg。生成器必须验证锁定 source Git blob SHA1，并对全部 2190 帧的 delta 编解码 round-trip 成功后才允许 ESP build。

Codex 下载 exact-SHA `tdisplay-gp-firmware-<SOURCE_SHA>` 并确认：

```text
manifest source_sha == approved SHA
actual firmware.bin SHA256 == manifest firmware_sha256
```

正常升级仅写 `firmware.bin` 到 manifest `firmware_offset`（当前通常 `0x10000`），Bad Apple 媒体已包含在 application image 内。不得 erase NVS，不重写 bootloader/partition table。Serial 115200。

## Display / Input

- 320×170 landscape rotation 3，无裁切/花屏/残留。
- 中文可读。
- 菜单六项：股票、天气、辉光时钟、智能家居、加密货币、设备信息。
- 40 ms debounce、700 ms long、long release 不产生额外 short。

## 默认启动 / Navigation

重启必须直接进入 **NixieClock**。

自动 idle 跳转已取消。分别在以下页面无按键停留 >60 s：

```text
Menu             -> still Menu
Weather          -> still Weather
Home Assistant   -> still Home Assistant
Crypto           -> still Crypto
DeviceInfo       -> still DeviceInfo
Stock            -> still Stock
Nixie            -> still Nixie
```

期间网络刷新/数据变化不得自动切换 App。App 只允许由用户明确按键导航切换。

## NixieClock

- 未同步时明确 waiting，不显示伪有效 1970/00:00。
- 同步后 HH:MM、日期、weekday、seconds 正确。
- 暖橙辉光，colon ~500 ms。
- 无 500 ms 整屏 black flash；minute change 无整屏闪烁。
- 不产生由 Nixie 触发的 `[md]` / `[appdata]`。
- `[sys] app=NIXIE_CLOCK`。

## Weather / Bad Apple

先配置有效地点并确认 `[appdata] type=WEATHER` 正常。

左侧信息必须满足：

- current condition、temperature、apparent temperature、humidity、wind、precipitation probability、updated time 可读且不被视频覆盖。
- 底部仅显示紧凑的 **“今” / “明”** 两行 high-low/condition。
- **不得显示“后天”** 卡片/行。

右侧视频必须满足：

- 可视区域为 **168×126**，从约 `x=152, y=27` 铺到屏幕右侧/接近底部，无越界。
- 黑白 Bad Apple silhouette 动画可辨认，不应只是噪点/全黑/全白。
- 帧率体感约 10 FPS，连续播放；不能每 100 ms 整屏闪黑/清屏。
- 保持 Weather 前台约 **220 s**，确认完整约 219 s 序列后能回到开头并继续循环。
- 长按 GPIO0 离开 Weather 后，当前页面不得再被 Bad Apple 后台重绘/污染。
- 再次进入 Weather 时从视频开头重新播放。
- 视频播放不应触发额外 `[net]` / `[appdata]` 请求；Open-Meteo 仍仅按原刷新策略工作。
- Weather 播放 5 min 期间无 watchdog/panic/reboot/freeze，heap 不持续单向下降。

天气网络回归：成功取得天气后安全断网，旧 cache 保留且无 tight retry；Bad Apple 本地视频仍可继续播放，因为媒体已经在 firmware.bin 内。

## Home Assistant

**使用用户已有 Home Assistant Server；T-Display 只是 REST client。**

T-Display 客户端配置页：

```text
http://<device-ip>:8081/
```

配置 existing HA Base URL、Long-Lived Access Token、1–4 entity IDs、optional labels、30–300 s refresh；HTTPS 才填 CA PEM。`:8081` 不是 HA Server。

HTTP 环境（如用户现有 LAN HA）：

```text
http://homeassistant.local:8123
http://<ha-ip>:8123
```

确认无需 CA 即可读取实体，UI 显示 state/unit/label，日志出现 `mode=HA_HTTP` 和 `[appdata] type=HOME_ASSISTANT`。HTTP Token 在 LAN 明文，仅可信 LAN 测试。

若有 HTTPS 环境：配置正确 CA 后读取成功且 `mode=HA_CA`；错误/缺失 CA 不得 insecure fallback。

Secret 检查：

- `/api/ha/status` 不含 Token/CA 内容，只允许 `ha_token_set` / `ha_ca_set`。
- Serial 不出现 Token/Authorization header。

成功取得 cache 后中断 HA/网络，已有实体保留 last state。V1 不测试写控制，因为明确 read-only。

## Crypto

- BTC / ETH / SOL 三行。
- USDT price + 24h change。
- `[appdata] type=CRYPTO`。
- `[net] host=data-api.binance.vision ...`。
- 前台停留 >60s 正常刷新，无 tight loop，且不得自动切到 Nixie。
- 成功后安全断网，last complete snapshot 保留。
- 退出 Crypto 后不启动新 Crypto cycles。

## DeviceInfo

真实 LAN IP、SSID/RSSI/MAC、uptime/time、heap/min heap、PSRAM、`Web: http://<IP>/`；不显示 secrets。

## Stock regression

Stock >60s 保持 Stock，直到用户主动离开。切股一次一只；正红负绿；quote/intraday 信息和图表正常；Tencent 默认 primary；每个新 intraday cycle 先 Tencent；quote/intraday health 独立。

正常常见：`Q:TX I:TX`。Tencent intraday 最终失败且 EastMoney 健康时应有 `[md] ... fallback=TX->EM` 并可出现 `I:EM`。Quote failover/recovery 逻辑不回归。

## Shared worker / NetworkArbiter

快速切 Weather/HA/Crypto/Menu/Nixie/Stock：

- actual external HTTP/TLS max one at once。
- Weather/HA/Crypto 共用一个 AppDataWorker，但 late results 不串 App。
- inactive app late completion 不 redraw current TFT。
- 显式离开 remote app 后，不允许 inactive app 启动新的 request cycle。
- Bad Apple 播放不得新增 FreeRTOS worker/AppDataWorker request/NetworkArbiter traffic。
- Nixie/DeviceInfo 不占 AppDataWorker/NetworkArbiter。

## Stability

收集 `[sys]`：

```text
app=MENU|STOCK|WEATHER|NIXIE_CLOCK|HOME_ASSISTANT|CRYPTO|DEVICE_INFO
```

至少覆盖开机、Stock 10 min、Weather Bad Apple >=5 min、Weather network cache test、HA success、Crypto >=2 cycles、Nixie 5 min、100 transitions、Wi-Fi interruption/recovery。

100 次跨 App/menu transition：watchdog=0、unexpected reboot=0、freeze=0、short-after-long=0、明显 heap leak=0、background wrong-screen redraw=0。

## Final report

```text
SOURCE SHA:
ACTIONS RUN / ARTIFACT:
PORT:
FIRMWARE SHA256:
FLASH: PASS/FAIL
DISPLAY/INPUT: PASS/FAIL
NIXIE DEFAULT START: PASS/FAIL
NO AUTO-IDLE SWITCH: PASS/FAIL
WEATHER LEFT LAYOUT: PASS/FAIL
BAD APPLE 168x126: PASS/FAIL
BAD APPLE ~10FPS: PASS/FAIL
BAD APPLE ~219S LOOP: PASS/FAIL
BAD APPLE EXIT/REENTER: PASS/FAIL
WEATHER NETWORK/CACHE: PASS/FAIL
HOME ASSISTANT HTTP: PASS/FAIL/NOT TESTED
HOME ASSISTANT HTTPS CA: PASS/FAIL/NOT TESTED
HA SECRET LEAK: PASS/FAIL
CRYPTO: PASS/FAIL
STOCK/TENCENT: PASS/FAIL
TX->EM FALLBACK: PASS/FAIL/NOT TRIGGERED
100-TRANSITION SOAK: PASS/FAIL
HEAP/STABILITY: PASS/FAIL
FULL HARDWARE ACCEPTANCE: PASS/FAIL/PARTIAL
```

FAIL 附原始串口、复现步骤、时间点和照片/截图（适用时）。
