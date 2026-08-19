# T-Display GP 部署与烧录指南

用于人工或 Codex 从 GitHub 部署到 **LILYGO T-Display-S3**。

## 1. 唯一准本

- Repository: `dgcary/t-display-gp`
- 默认部署已批准的 `main`
- 用户指定 branch/SHA 时严格部署该 ref
- 每次真机验收记录完整 40 位 SHA

不要用旧 `.pio`、旧 `firmware.bin` 或旧工程目录代替 GitHub 当前源码。

## 2. 获取代码

```bash
git fetch origin
git checkout <branch>
git pull --ff-only origin <branch>
git rev-parse HEAD
git status
```

工作区必须干净；不要擅自 reset/drop 未提交内容。

## 3. 部署前强制检查

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
```

任一失败：**禁止烧录**。

CI 同时验证 Ubuntu native 与 Windows native。TFT/UI contract 还会验证 rotation 3、WeatherVisuals native wiring、Weather Unicode 条件文本以及股票独立报价/分时 Provider 页脚接线。

## 4. 烧录

```bash
pio device list
pio run -e lilygo-t-display-s3 -t upload --upload-port COM6
pio device monitor --port COM6 -b 115200
```

COM6 是本项目当前常用 Windows 端口；实际执行前必须确认目标串口确实是 T-Display-S3。

## 5. 配置迁移

当前配置 schema 为 v2，但继续使用 `stockticker` NVS namespace。

从旧 schema v1 升级时应自动保留：

- 股票代码
- 股票显示名
- 股票刷新周期

新天气配置默认关闭、15 分钟刷新。读取旧配置成功后固件会 best-effort 写回 v2。

升级前后都不要主动擦除 NVS，除非测试目标明确要求“首次配网”。

## 6. 首次配网 / Web 设置

无有效配置时：

1. 连接 `TDisplay-GP-Setup`
2. 打开 Captive Portal；必要时访问 `192.168.4.1`
3. 选择 Wi-Fi
4. 输入 3–5 只股票
5. 设置 3/4/5 秒股票刷新
6. 可选启用天气
7. 启用天气时填写地点名称、纬度、经度、5–60 分钟刷新
8. 保存
9. 设备受控重启

已经联网但不知道设备 IP 时：

```text
长按 GPIO0 -> 主菜单 -> 设备信息 -> 长按 GPIO14
```

设备信息页会直接显示 `Web: http://<IP>/`。用同一局域网浏览器访问即可进入 Web 配置页。

## 7. App 操作

### 普通 App

```text
GPIO0 短按  -> 上一项
GPIO14 短按 -> 下一项
GPIO0 长按  -> 返回主菜单
GPIO14 长按 -> 保留/无动作
```

长按阈值 700 ms。

### 主菜单

```text
GPIO0 短按  -> 上一个 App
GPIO14 短按 -> 下一个 App
GPIO0 长按  -> 无动作
GPIO14 长按 -> 进入选中 App
```

当前菜单至少包含：`股票`、`天气`、`设备信息`。设备开机默认进入 StockApp。

## 8. 串口日志

### 股票

```text
[md] id=... type=QUOTE|INTRADAY|PROBE symbol=... provider=EM|TX attempt=... queue=...ms dur=...ms http=... native=... tls=... bytes=.../... result=...
```

东财分时交给腾讯时还应看到：

```text
[md] id=... type=INTRADAY symbol=... fallback=EM->TX
```

### 天气 / App 数据

```text
[appdata] id=... type=WEATHER location=... queue=...ms dur=...ms http=... native=... tls=... bytes=.../... result=...
```

### 系统资源

约每 60 秒：

```text
[sys] app=STOCK|MENU|WEATHER|DEVICE_INFO heap_free=... heap_min=... psram_free=... psram_total=... main_stack_hwm=...
```

## 9. 多 App Smoke Test

烧录后按顺序确认：

1. 320×170 横屏方向正确，无裁切/花屏
2. 开机默认出现股票页面
3. GPIO0/GPIO14 短按仍只切一只股票
4. 长按 GPIO0 返回主菜单，释放时不额外触发股票切换
5. 菜单能在 `股票 / 天气 / 设备信息` 之间循环
6. 长按 GPIO14 能进入选中 App
7. DeviceInfo 显示真实 IP、SSID、RSSI、MAC、Heap、uptime
8. `Web: http://<IP>/` 能从同一 LAN 访问
9. Weather 显示地点、当前天气、三日高低温和中文天气状态
10. 右侧天气小猫持续两帧轻动画，无整屏闪烁/花屏
11. 返回股票后旧股票缓存立即可见
12. 串口无 watchdog / Guru Meditation / 异常 reboot

## 10. 股票分时 fallback 检查

新的分时行为与报价 failover 独立：

```text
新分时周期
  -> EastMoney intraday
  -> transient 时按既有有限重试
  -> 最终失败/非重试错误
  -> Tencent minute fallback
```

真机观察：

- 每个新周期仍先 `provider=EM`
- EM 最终失败后出现 `fallback=EM->TX`
- 腾讯成功后图表出现/更新
- 页脚 `I:TX`
- 报价来源单独显示为 `Q:EM` 或 `Q:TX`
- Tencent intraday 成功/失败均不得改变 quote failover 健康
- EM+TX 都失败时旧图保留且不会紧密循环；下个周期等待正常分时刷新间隔后重新从 EM 开始

## 11. 网络隔离检查

外部股票与天气 HTTPS 必须通过共享 NetworkArbiter 串行执行。

- 不应出现因多个 TLS 同时执行导致的明显内存异常
- StockApp 退出后不应继续启动新的 pending 行情工作
- 已经执行中的股票 HTTPS 可以自然结束
- Weather 失败不得改变股票 Provider/健康状态
- Weather 失败后不得形成连续快速请求
- DeviceInfo 不发起外部网络请求

## 12. 稳定性验收

最低新增多 App 稳定性目标：

```text
Stock -> Menu -> Weather -> Menu -> DeviceInfo -> Menu -> Stock 循环 >=100 次
short-after-long 误触发 = 0
watchdog = 0
unexpected reboot = 0
freeze = 0
cache loss = 0
明显持续 Heap 泄漏 = 0
```

股票原有交易时段量化目标与腾讯分时 fallback 细节见 `hardware-acceptance.md`。

## 13. 状态判定

分别记录：

```text
HOST TEST PASS
WINDOWS NATIVE PASS
FIRMWARE BUILD PASS
FLASH PASS
DEVICE INFO PASS
WEATHER LIVE/UI PASS
TENCENT INTRADAY FALLBACK PASS
STOCK STABILITY PASS
FULL HARDWARE ACCEPTANCE PASS
```

前一级不能自动替代后一级。

## 14. 常见问题

### 天气显示“未配置”

进入设备信息查看 IP，在浏览器打开 `http://<IP>/`，启用天气并填写地点/经纬度/刷新周期，保存等待受控重启。

### 天气失败但旧数据还在

这是预期降级：保留旧缓存并显示天气错误/延迟。检查 `[appdata]`，不要通过高频无限重试修复。

### 报价正常但分时图不更新

先看 `[md]`。若 EastMoney `trends2` TLS/HTTP 失败，应继续观察是否出现 `fallback=EM->TX` 以及后续 `provider=TX type=INTRADAY`。不要因为东财不稳定就放宽 Parser 或影响已经正常工作的腾讯报价。

### 返回菜单后仍看到一条股票请求完成

允许：退出不会强杀已经开始的 HTTPS。新的 pending/retry 在 StockApp suspended 状态下不会继续执行；已执行请求完成后只进入缓存/结果队列，不得后台绘屏。

完整真机验收见 [hardware-acceptance.md](hardware-acceptance.md)。
