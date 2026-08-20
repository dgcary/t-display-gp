# T-Display-S3 Hardware Acceptance

本清单只用于 **LILYGO T-Display-S3 真机**。Host tests / firmware build 不能代替实体板验收。

## 当前状态

- 自动化 validators / native tests：网页版 ChatGPT + GitHub Actions 验证
- Windows native：GitHub Actions 验证
- ESP32-S3 firmware build：网页版 ChatGPT + GitHub Actions 验证
- verified firmware Artifact：GitHub Actions 生成并由网页版 ChatGPT确认
- 实体 flash / NixieClock / DeviceInfo / Weather watercolor pet / Tencent-primary market path / App 切换稳定性：**PENDING，必须在真机完成**

每次真机验收记录：branch、完整 source SHA、Actions run/artifact、日期、Wi-Fi 环境、端口和结果。

## 1. Artifact / Flash

### 开发侧（网页版 ChatGPT）

以下已经在交给 Codex 前完成：

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_nixie_clock_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
```

CI 必须生成：

```text
tdisplay-gp-firmware-<SOURCE_SHA>
```

其中包含：

```text
firmware.bin
partitions.bin
bootloader.bin
firmware-manifest.txt
```

### Codex 本地

Codex **不再运行 `pio run` 做本地编译门禁**。

下载 exact-SHA Artifact 后检查：

```powershell
Get-Content .\firmware-artifact\firmware-manifest.txt
(Get-FileHash .\firmware-artifact\firmware.bin -Algorithm SHA256).Hash.ToLower()
```

必须满足：

```text
manifest source_sha == approved exact SHA
actual firmware SHA256 == manifest firmware_sha256
```

然后确认串口：

```bash
pio device list
```

使用 PlatformIO 已安装的 esptool，只写应用 image 到 manifest 的 `firmware_offset`（当前 `0x10000`）：

```powershell
$pioPython = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$esptool = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
& $pioPython $esptool --chip esp32s3 --port <PORT> --baud 460800 write_flash 0x10000 .\firmware-artifact\firmware.bin
```

随后：

```bash
pio device monitor --port <PORT> -b 115200
```

正常升级禁止 erase flash/NVS，也不重写 bootloader/partition table。

要求：无启动循环、panic、watchdog、NetworkArbiter/MarketDataWorker/AppDataWorker 启动失败。

正常完成应出现：

```text
[boot] multi-app loop ready
```

## 2. 屏幕与硬件不变量

- GPIO15 屏幕供电 HIGH
- GPIO38 背光
- GPIO0 / GPIO14 active-low pull-up
- ST7789 物理 170×320
- 应用逻辑 **320×170 landscape，rotation 3**

通过标准：

- 方向正确，无裁切/花屏
- 中文文本可读
- 主菜单、股票、天气、辉光时钟、设备信息都在完整 320×170 区域内
- 无明显残影/跨 App 旧 UI 未清除

## 3. 按键与菜单

长按阈值 700 ms，防抖 40 ms。

### StockApp

- GPIO0 短按：上一只股票
- GPIO14 短按：下一只股票
- GPIO0 长按：返回主菜单
- GPIO14 长按：无动作

### NixieClockApp

- GPIO0 短按：当前无动作
- GPIO14 短按：当前无动作
- GPIO0 长按：返回主菜单
- GPIO14 长按：无动作

### MenuApp

- GPIO0 短按：上一个 App
- GPIO14 短按：下一个 App
- GPIO0 长按：无动作
- GPIO14 长按：进入选中的 App

菜单应至少可见：

```text
股票
天气
辉光时钟
设备信息
```

验证：

- 长按只触发一次
- 长按释放时不再触发一次短按
- 持续按住不自动重复
- 100 次交互中无明显双触发/漏触发

## 4. 启动与 App 生命周期

开机默认进入 StockApp。

执行：

```text
Stock -> Menu -> Weather -> Menu -> NixieClock -> Menu -> DeviceInfo -> Menu -> Stock
```

要求：

- 每次进入 App 都能立即绘制
- 返回股票时最后有效股票缓存仍存在
- 返回天气时最后有效天气缓存仍存在
- NixieClock 重新进入时读取当前本地时间，不保留冻结的旧秒数
- 非前台 App 不抢 TFT 绘制
- NixieClock / DeviceInfo 不触发外部网络请求
- 切换不触发 Wi-Fi 重连

## 5. 配置 schema v1 -> v2 迁移

在有旧版有效股票配置的设备上升级，不擦除 NVS。

要求：

- 原股票数量/代码不变
- 原显示名不变
- 原 3/4/5 秒刷新周期不变
- 天气默认关闭
- Web `/api/status` 可看到 v2 字段
- 无需重新输入股票
- NixieClock 不增加配置字段、不要求重新配网

迁移失败时保存串口日志和旧配置证据，不要直接擦 NVS 掩盖问题。

## 6. Nixie Clock / 辉光时钟验收

进入“辉光时钟”。

时间已同步时应看到：

- 四位 `HH:MM` 本地时间
- 日期 `YYYY-MM-DD`
- 英文星期缩写 `SUN..SAT`
- 当前秒数 `SEC xx`
- 暗色玻璃管/内腔效果
- 暖橙/琥珀色多层数字辉光
- 冒号约 500 ms 相位明灭

视觉/刷新要求：

- 时间必须与 DeviceInfo/同一时区的可信时钟一致，允许正常刷新边界的约 1 秒误差
- 每分钟变更时只更新必要数字区域，不应整屏黑闪
- 冒号闪烁时不得每 500 ms 清空整屏
- 日期/秒数区域无明显残影
- 长时间停留无花屏、数字错位或边框逐渐破坏
- GPIO0 长按可以正常返回主菜单
- GPIO0/GPIO14 短按不会误切换 App 或制造无意义状态变化

未同步时间时：

```text
等待时间同步
```

应明确可见，不得显示 `1970-01-01`、`00:00` 等伪有效时间。完成系统时间同步后，无需重启即可自动进入正常显示。

网络隔离：

- 进入/停留/退出 NixieClock 不得直接产生新的 `[md]` / `[appdata]` 请求
- 不得占用 `AppDataWorker` / `NetworkArbiter`
- `[sys]` 在该页应显示 `app=NIXIE_CLOCK`

当前版本**不验收自动 Screensaver**；Screensaver 尚未实现。

## 7. DeviceInfo / Web 配置入口

进入“设备信息”：

应至少看到：

- 当前 LAN IP
- SSID
- RSSI
- MAC
- uptime
- Heap / minimum Heap / PSRAM
- 当前时间（NTP 已同步时）
- `Web: http://<IP>/`

用同一局域网手机/电脑打开页面显示的 `http://<IP>/`：

- 页面可访问
- 可看到股票/天气配置
- IP 与设备信息页一致
- DeviceInfo 页面不显示 Wi-Fi 密码或其他 secret

## 8. Weather 配置

局域网 Web 或首次 Captive Portal：

- 启用 Weather
- 填写地点名称
- 纬度 -90..90
- 经度 -180..180
- 刷新 5..60 分钟

保存后应受控重启。

非法测试：

- 纬度 >90
- 经度 >180
- 刷新 <5 或 >60
- 启用 Weather 但地点/经纬度缺失

都必须被拒绝，不写入部分配置。

## 9. Weather live + UI 验收

进入 WeatherApp：

- 显示配置地点名称
- 当前温度可见且重点着色
- 天气状态着色
- 体感温度可见
- 湿度可见
- 风速可见
- 降雨概率可见
- 今/明/后高低温可见
- 今/明/后中文天气状态可正常显示，不得乱码/方框
- 更新时间可见
- 数据不是永久停在“正在获取”

右侧天气角色采用用户已选定的 **方案 4：手绘水彩风小猫**。验收：

- 小猫使用奶油/暖棕、低饱和多层晕染效果，不应再像旧版单色几何橘猫
- 约 500 ms 两帧切换，可观察到轻微身体/尾巴动作
- 晴/多云/雾/雨/雪/雷雨背景元素与天气代码匹配
- 高温：汗滴
- 雾/困倦：睡眠符号
- 雨：雨伞
- 低温：围巾
- 雷暴：闪电/受惊表情
- 动画过程中左侧天气文字和底部三日卡片不应每 500 ms 整屏闪烁
- 无明显残影、花屏或长期黑块

只能对真机当时能自然触发的天气状态判 PASS；其他状态记为 `NOT TESTABLE`，不能仅凭代码测试冒充物理验收。

串口必须有：

```text
[appdata] id=... type=WEATHER location=... queue=...ms dur=...ms http=... native=... tls=... bytes=.../... result=...
```

## 10. Weather 失败与缓存

先获得一份有效天气，再制造安全的网络中断/离线场景。

要求：

- 旧天气保持显示
- 显示天气专属错误/延迟状态
- 不白屏
- 不清空最后有效快照
- 不出现快速连续天气重试风暴
- 恢复网络后按正常刷新周期自动恢复
- Weather 失败不改变股票报价/分时 Provider 状态

## 11. TLS 串行化

股票和天气外部 HTTPS 共享 NetworkArbiter；NixieClock / DeviceInfo 不进入该路径。

快速 Stock/Menu/Weather/NixieClock/DeviceInfo 切换并观察：

- 同一时刻不应出现多个外部 TLS 导致的明显 Heap 崩落
- StockApp 退出后 pending 行情不继续执行
- 已经开始的股票 HTTPS 可自然完成
- in-flight 完成不得让后台 StockApp 重新绘屏
- Weather 请求可在共享网络资源可用后正常执行
- NixieClock 不应占用 NetworkArbiter
- DeviceInfo 不应占用 NetworkArbiter

## 12. `[sys]` 资源验收

约每 60 秒记录：

```text
[sys] app=STOCK|MENU|WEATHER|NIXIE_CLOCK|DEVICE_INFO heap_free=... heap_min=... psram_free=... psram_total=... main_stack_hwm=...
```

至少收集：

- 开机后稳定值
- Stock 运行 10 分钟
- Weather 成功请求并持续动画后
- NixieClock 页面至少 5 分钟
- DeviceInfo 页面
- 100 次 App 切换后
- Wi-Fi 中断/恢复后

通过原则：

- 无持续单向 `heap_free` 下降趋势
- `heap_min` 可反映历史峰值，但不应逼近明显危险水平
- `main_stack_hwm` 不应持续下降到接近 0
- 无分配失败、panic、watchdog

## 13. 股票回归 / 正常腾讯主链路

多 App 版本仍必须通过原股票 Smoke，并验证腾讯已经成为默认主源：

- 正涨红、下跌绿
- 中文名称可读
- 当前价、开/高/低/昨、量/额正常
- 分时图正常
- `昨收` / `今开` 参考线可区分
- 午休断点正确
- GPIO0/GPIO14 短按一次只切一次
- 分时失败旧图保留
- Quote/Intraday 健康独立
- 启动/正常网络下报价首先使用 Tencent
- 每个新的分时周期首先使用 Tencent

常态预期页脚：

```text
Q:TX I:TX
```

`Q:` 是当前报价来源，`I:` 是当前有效分时缓存来源。若尚无分时缓存，可只显示 `Q:TX`。

## 14. Quote failover：Tencent -> EastMoney

只有在腾讯报价真实失败时测试；不要为触发 fallback 修改源码或破坏外部网络基础设施。

策略：

1. 默认 `Q:TX`。
2. 60 秒 failure window 内累计达到 3 次 Tencent quote failure 后，报价切到 EastMoney。
3. fallback 期间报价来源应显示 `Q:EM`（若 EastMoney 自身可用）。
4. fallback 期间按约 120 秒周期探测 Tencent。
5. 连续 2 次 Tencent probe 成功后恢复 `Q:TX`。
6. probe 失败会重置 recovery success count。
7. EastMoney fallback 也失败时，不继续递归寻找第三 Provider；保留最后有效 quote cache。

如果本次真机中 Tencent 始终稳定，该路径记录：

```text
QUOTE FALLBACK TX->EM: NOT TRIGGERED
```

## 15. Intraday fallback：Tencent -> EastMoney

新的正常路径为 Tencent minute first。

保持正常 StockApp 前台运行并观察 `[md]`：

1. 新分时周期首先应请求 `provider=TX`。
2. Tencent 可重试 transient 错误按既有有限重试执行，最多 3 次总尝试。
3. Tencent 周期最终失败或遇到非重试型 Provider error 后，应出现：

```text
[md] id=... type=INTRADAY symbol=... fallback=TX->EM
```

4. 随后应看到同一逻辑周期的 EastMoney intraday 请求。
5. EastMoney 成功后：
   - 分时图出现/更新；
   - 旧图不会先被清空；
   - footer `I:EM`；
   - quote provider 不因该成功自动改变。
6. 若 EastMoney 也失败：
   - 不递归 fallback；
   - 不进入 tight loop；
   - 旧有效分时缓存保留；
   - 完整失败后等待正常分时刷新间隔再从 Tencent 开新周期。

主要真机成功标准是 `Q:TX I:TX` 能持续工作。`TX->EM` fallback 只有在 Tencent 自然故障时完整物理验证；未触发时写 `NOT TRIGGERED`。

## 16. 股票请求日志

```text
[md] id=... type=QUOTE|INTRADAY|PROBE symbol=... provider=EM|TX attempt=... queue=...ms dur=...ms http=... native=... tls=... bytes=.../... result=...
```

保留失败前后至少 30 秒日志，并结合同时期 `[sys]` 判断。

## 17. Wi-Fi 中断

加载有效股票和天气后断 Wi-Fi 至少 2 分钟：

- 当前 App 仍可操作
- 已有股票/天气缓存保留
- 菜单/NixieClock/DeviceInfo 仍响应
- NixieClock 使用已有系统时间继续正常走时（不把断 Wi-Fi 当作立即无效时间）
- UI 不等待 HTTP 卡死
- Wi-Fi 恢复后无需人工重启即可继续后续请求

## 18. 多 App 稳定性量化

至少：

```text
Stock -> Menu -> Weather -> Menu -> NixieClock -> Menu -> DeviceInfo -> Menu -> Stock 循环 >=100 次
button short-after-long = 0
watchdog = 0
unexpected reboot = 0
freeze = 0
stock cache loss = 0
weather cache loss = 0
Nixie 500ms full-screen flicker = 0
monotonic heap leak = 0
```

建议同时混入股票短按切换，使输入状态机覆盖真实使用。

## 19. 股票交易时段量化

目标：

```text
股票切换 >=100 次
quote 请求 >=500 次
Tencent primary quote 成功率目标 >=99%（测试网络条件）
intraday 刷新周期 >=30 个
Tencent->EastMoney 组合分时周期成功率目标 >=80%
健康 quote Provider 下当前报价 P95 更新间隔 <=7 秒
单次 intraday 失败时 quote gap <=10 秒
intraday P95 age <=180 秒
watchdog = 0
unexpected reboot = 0
cache loss = 0
```

若腾讯 primary 本身在测试网络中持续低于目标，应先基于真实 `[md]` 证据重新评估 Provider；不要无限重试或放宽 parser。

## 20. 完整交易日

最终仍建议至少运行一次 **09:25–15:10**，期间穿插进入 Weather/Menu/NixieClock/DeviceInfo：

- 开盘前
- 上午盘
- 午休
- 下午盘
- 收盘

要求无异常重启/卡死/缓存丢失，收盘后保留最终报价与分时图。

## Acceptance record

```text
Repository: dgcary/t-display-gp
Branch:
Source SHA:
Actions run:
Artifact name/id:
Artifact source_sha:
Firmware SHA256 verified:
Board: LILYGO T-Display-S3
Port:
Wi-Fi:
Date:
Web/CI validators:
Ubuntu native:
Windows native:
Firmware build:
Artifact verification:
Flash:
Menu/Input:
Nixie Clock UI/local-time:
Nixie local-only/network isolation:
DeviceInfo/IP:
Weather live:
Weather watercolor pet/UI:
Tencent primary quote:
Tencent primary intraday:
Quote TX->EM fallback:
Intraday TX->EM fallback:
Stock regression:
100-switch stability:
Heap/resource notes:
Full-day:
Notes:
```

没有实体板证据的功能必须保持 **PENDING**。
