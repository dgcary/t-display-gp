# T-Display GP 部署与烧录指南

用于从 GitHub 将 **已经由网页版 ChatGPT 编译验证完成的固件**部署到 **LILYGO T-Display-S3**。

## 1. 固定职责分工

本项目固定采用：

```text
网页版 ChatGPT
  -> 代码开发 / 测试 / GitHub 提交
  -> PlatformIO validators + native tests
  -> pio run 编译 ESP32-S3 firmware
  -> GitHub Actions 上传 verified firmware artifact

Codex
  -> 下载 exact-SHA artifact
  -> 校验 manifest / SHA256
  -> 本地烧录
  -> 串口监控
  -> 真机功能测试
  -> 把问题证据反馈给网页版 ChatGPT
```

**Codex 不再承担常规本地编译和编译排错。** 真机发现问题后也不要现场改源码；回传日志/照片/复现步骤，由网页版 ChatGPT 修改并重新编译。

## 2. 唯一准本

- Repository: `dgcary/t-display-gp`
- 默认部署已批准的 `main`
- 用户指定 branch/SHA 时严格部署该 ref
- 每次真机验收记录完整 40 位 source SHA
- GitHub Actions 的 verified firmware artifact 必须与该 SHA 完全对应

不要用旧 `.pio`、旧 `firmware.bin` 或旧工程目录代替指定 GitHub Artifact。

## 3. 开发侧强制验证

以下步骤由网页版 ChatGPT / CI 完成，不是 Codex 的本地部署门禁：

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
```

全部成功后 CI 上传：

```text
tdisplay-gp-firmware-<SOURCE_SHA>
```

Artifact 至少包含：

```text
firmware.bin
partitions.bin
bootloader.bin
firmware-manifest.txt
```

`firmware-manifest.txt` 包含：

```text
repository
source_sha
workflow_sha
environment
firmware_offset
firmware_sha256
partitions_sha256
bootloader_sha256
```

## 4. Codex 下载预编译 Artifact

Codex 只下载已经通过 CI 的 exact-SHA Artifact。例如：

```powershell
gh run download <RUN_ID> `
  -n tdisplay-gp-firmware-<SOURCE_SHA> `
  -D .\firmware-artifact
```

下载后必须检查：

```powershell
Get-Content .\firmware-artifact\firmware-manifest.txt
(Get-FileHash .\firmware-artifact\firmware.bin -Algorithm SHA256).Hash.ToLower()
```

要求：

- `source_sha` == 用户指定的 exact SHA
- 实际 `firmware.bin` SHA256 == manifest 的 `firmware_sha256`

任一不一致：**禁止烧录**。

## 5. 确认设备串口

```bash
pio device list
```

COM6 是本项目常用 Windows 端口，但每次必须确认 VID/PID/设备信息属于目标 T-Display-S3。

不要触碰其他串口。

## 6. 烧录预编译应用固件

当前项目 partition layout 未改变；CI manifest 指定应用 offset（当前为 `0x10000`）。正常升级只写 `firmware.bin` 到该 offset，以保留 bootloader、partition table、NVS/Wi-Fi/股票/天气配置。

Windows + PlatformIO 环境推荐直接使用已有 PlatformIO esptool：

```powershell
$pioPython = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$esptool = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"

& $pioPython $esptool `
  --chip esp32s3 `
  --port COM6 `
  --baud 460800 `
  write_flash `
  0x10000 `
  .\firmware-artifact\firmware.bin
```

实际 offset 必须以本次 Artifact 的 `firmware-manifest.txt` 为准；不要硬编码其他值。

禁止：

```text
erase_flash
erase NVS
重写 partitions.bin
重写 bootloader.bin
```

除非有单独批准的恢复/首次烧录任务。

## 7. 串口监控

烧录完成后：

```bash
pio device monitor --port COM6 -b 115200
```

正常启动应出现：

```text
[boot] multi-app loop ready
```

重点收集：

```text
[md]
[appdata]
[sys]
Guru Meditation
watchdog
panic
abort
reboot
```

## 8. 配置迁移

当前配置 schema 为 v2，但继续使用 `stockticker` NVS namespace。

从旧 schema v1 升级时应自动保留：

- 股票代码
- 股票显示名
- 股票刷新周期

新天气配置默认关闭、15 分钟刷新。读取旧配置成功后固件会 best-effort 写回 v2。

升级前后都不要主动擦除 NVS，除非测试目标明确要求“首次配网”。

## 9. 首次配网 / Web 设置

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

设备信息页会直接显示 `Web: http://<IP>/`。

## 10. App 操作

### 普通 App

```text
GPIO0 短按  -> 上一项
GPIO14 短按 -> 下一项
GPIO0 长按  -> 返回主菜单
GPIO14 长按 -> 保留/无动作
```

### 主菜单

```text
GPIO0 短按  -> 上一个 App
GPIO14 短按 -> 下一个 App
GPIO0 长按  -> 无动作
GPIO14 长按 -> 进入选中 App
```

当前菜单至少包含：`股票`、`天气`、`设备信息`。设备开机默认进入 StockApp。

## 11. 串口日志

### 股票

```text
[md] id=... type=QUOTE|INTRADAY|PROBE symbol=... provider=EM|TX attempt=... queue=...ms dur=...ms http=... native=... tls=... bytes=.../... result=...
```

正常情况下报价和分时都优先走腾讯。腾讯分时周期最终交给东方财富时应看到：

```text
[md] id=... type=INTRADAY symbol=... fallback=TX->EM
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

## 12. 多 App Smoke Test

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
10. 右侧**手绘水彩风小猫**持续两帧轻动画，无整屏闪烁/花屏
11. 返回股票后旧股票缓存立即可见
12. 串口无 watchdog / Guru Meditation / 异常 reboot

## 13. 股票 Provider / 分时 fallback 检查

当前策略：

```text
报价：
Tencent primary
  -> 连续失败达到阈值
  -> EastMoney fallback
  -> 周期性探测 Tencent
  -> 连续恢复成功后回 Tencent

分时：
Tencent intraday
  -> transient 时按既有有限重试
  -> 最终失败/非重试错误
  -> EastMoney intraday fallback
```

真机观察：

- 正常状态优先出现 `provider=TX`
- 新分时周期先 `provider=TX`
- Tencent 最终分时失败后出现 `fallback=TX->EM`
- EastMoney 成功后图表出现/更新，页脚 `I:EM`
- 正常腾讯分时成功时页脚 `I:TX`
- 报价来源单独显示为 `Q:TX` 或 fallback `Q:EM`
- Intraday 成功/失败不得改变 quote failover 健康
- TX+EM 分时都失败时旧图保留且不会 tight loop

## 14. 网络隔离检查

外部股票与天气 HTTPS 必须通过共享 NetworkArbiter 串行执行。

- StockApp 退出后不应继续启动新的 pending 行情工作
- 已经执行中的股票 HTTPS 可以自然结束
- Weather 失败不得改变股票 Provider/健康状态
- Weather 失败后不得形成连续快速请求
- DeviceInfo 不发起外部网络请求

## 15. 稳定性验收

```text
Stock -> Menu -> Weather -> Menu -> DeviceInfo -> Menu -> Stock 循环 >=100 次
short-after-long 误触发 = 0
watchdog = 0
unexpected reboot = 0
freeze = 0
cache loss = 0
明显持续 Heap 泄漏 = 0
```

## 16. 状态判定

分别记录：

```text
WEB/CI VALIDATORS PASS
UBUNTU NATIVE PASS
WINDOWS NATIVE PASS
FIRMWARE BUILD PASS
VERIFIED ARTIFACT PASS
FLASH PASS
DEVICE INFO PASS
WEATHER LIVE/UI PASS
TENCENT PRIMARY PASS
TX->EM FALLBACK PASS / NOT TRIGGERED
STOCK STABILITY PASS
FULL HARDWARE ACCEPTANCE PASS
```

编译/Artifact PASS 由网页版 ChatGPT 提供；Codex 不重复本地编译来证明它。

## 17. 常见问题

### Codex 本地 `pio run` 卡住

按固定工作流，Codex 不应依赖本地 `pio run`。只要网页版 ChatGPT 已给出 exact-SHA verified artifact，直接下载、校验并用 esptool 烧录。

### 天气显示“未配置”

进入设备信息查看 IP，在浏览器打开 `http://<IP>/`，启用天气并填写地点/经纬度/刷新周期。

### 腾讯报价或分时异常

先看 `[md]`。正常路径应优先 `provider=TX`。若 Tencent 分时最终失败，应观察 `fallback=TX->EM` 和后续 `provider=EM type=INTRADAY`。

### 东方财富备用仍然失败

允许。EastMoney 现在只是 secondary；若腾讯正常，主行情不应受影响。

### 返回菜单后仍看到一条股票请求完成

允许：退出不会强杀已经开始的 HTTPS。新的 pending/retry 在 StockApp suspended 状态下不会继续执行。

完整真机验收见 [hardware-acceptance.md](hardware-acceptance.md)。
