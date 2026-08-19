# T-Display-S3 Hardware Acceptance

本清单只用于 **LILYGO T-Display-S3 真机**。Host tests / firmware build 不能代替实体板验收。

## 当前状态

- 自动化 native tests：GitHub Actions 验证
- Windows native：GitHub Actions 验证
- ESP32-S3 firmware build：GitHub Actions 验证
- 实体 flash / DeviceInfo / Weather pet / Tencent intraday fallback / App 切换稳定性：**PENDING，必须在真机完成**

每次真机验收记录：branch、完整 commit SHA、日期、Wi-Fi 环境、端口和结果。

## 1. Build / Flash

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
pio test -e native
pio run -e lilygo-t-display-s3
pio run -e lilygo-t-display-s3 -t upload --upload-port <PORT>
pio device monitor --port <PORT> -b 115200
```

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
- 主菜单、股票、天气、设备信息都在完整 320×170 区域内
- 无明显残影/跨 App 旧 UI 未清除

## 3. 按键与菜单

长按阈值 700 ms，防抖 40 ms。

### StockApp

- GPIO0 短按：上一只股票
- GPIO14 短按：下一只股票
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
Stock -> Menu -> Weather -> Menu -> DeviceInfo -> Menu -> Stock
```

要求：

- 每次进入 App 都能立即绘制
- 返回股票时最后有效股票缓存仍存在
- 返回天气时最后有效天气缓存仍存在
- 非前台 App 不抢 TFT 绘制
- DeviceInfo 不触发外部网络请求
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

迁移失败时保存串口日志和旧配置证据，不要直接擦 NVS 掩盖问题。

## 6. DeviceInfo / Web 配置入口

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

## 7. Weather 配置

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

## 8. Weather live + UI 验收

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

右侧天气角色：

- 可见轻量像素/卡通小猫
- 约 500 ms 两帧切换，可观察到轻微身体/尾巴动作
- 晴/多云/雾/雨/雪/雷雨背景元素与天气代码匹配
- 高温、寒冷、雨、雾、雷暴等场景的小猫表情/配件有对应变化
- 动画过程中左侧天气文字和底部三日卡片不应每 500 ms 整屏闪烁
- 无明显残影、花屏或长期黑块

串口必须有：

```text
[appdata] id=... type=WEATHER location=... queue=...ms dur=...ms http=... native=... tls=... bytes=.../... result=...
```

## 9. Weather 失败与缓存

先获得一份有效天气，再制造安全的网络中断/离线场景。

要求：

- 旧天气保持显示
- 显示天气专属错误/延迟状态
- 不白屏
- 不清空最后有效快照
- 不出现快速连续天气重试风暴
- 恢复网络后按正常刷新周期自动恢复
- Weather 失败不改变股票报价/分时 Provider 状态

## 10. TLS 串行化

股票和天气外部 HTTPS 共享 NetworkArbiter。

快速 Stock/Menu/Weather/DeviceInfo 切换并观察：

- 同一时刻不应出现多个外部 TLS 导致的明显 Heap 崩落
- StockApp 退出后 pending 行情不继续执行
- 已经开始的股票 HTTPS 可自然完成
- in-flight 完成不得让后台 StockApp 重新绘屏
- Weather 请求可在共享网络资源可用后正常执行
- DeviceInfo 不应占用 NetworkArbiter

## 11. `[sys]` 资源验收

约每 60 秒记录：

```text
[sys] app=STOCK|MENU|WEATHER|DEVICE_INFO heap_free=... heap_min=... psram_free=... psram_total=... main_stack_hwm=...
```

至少收集：

- 开机后稳定值
- Stock 运行 10 分钟
- Weather 成功请求并持续动画后
- DeviceInfo 页面
- 100 次 App 切换后
- Wi-Fi 中断/恢复后

通过原则：

- 无持续单向 `heap_free` 下降趋势
- `heap_min` 可反映历史峰值，但不应逼近明显危险水平
- `main_stack_hwm` 不应持续下降到接近 0
- 无分配失败、panic、watchdog

## 12. 股票回归

多 App 版本仍必须通过原股票 Smoke：

- 正涨红、下跌绿
- 中文名称可读
- 当前价、开/高/低/昨、量/额正常
- 分时图正常
- `昨收` / `今开` 参考线可区分
- 午休断点正确
- GPIO0/GPIO14 短按一次只切一次
- 分时失败旧图保留
- Quote/Intraday 健康独立
- EastMoney quote 失败后 Tencent quote fallback 语义不变

股票页脚应区分实际数据源：

```text
Q:EM
Q:TX I:EM
Q:TX I:TX
```

其中 `Q:` 是报价来源，`I:` 是当前有效分时缓存来源。

## 13. Tencent 分时 fallback 真机验收

本轮重点验证 EastMoney `trends2` 不稳定时的自动接管。

保持正常 StockApp 前台运行并观察 `[md]`：

1. 新分时周期首先应请求 `provider=EM`。
2. EastMoney 可重试的 transient 错误按既有有限重试执行。
3. EastMoney 周期最终失败后，应出现：

```text
[md] id=... type=INTRADAY symbol=... fallback=EM->TX
```

4. 随后应看到同一逻辑周期的 Tencent minute 请求。
5. Tencent 成功后：
   - 分时图出现/更新；
   - 旧图不会先被清空；
   - footer `I:TX`；
   - quote provider 不因该成功自动改变。
6. 若 Tencent 也失败：
   - 不递归 fallback；
   - 不进入 tight loop；
   - 旧有效分时缓存保留；
   - 完整失败后等待正常分时刷新间隔再从 EM 开新周期。

如果当前环境中 EastMoney 恰好恢复稳定，需记录为 `fallback path NOT TRIGGERED`，不得臆测 PASS。

## 14. 股票请求日志

```text
[md] id=... type=QUOTE|INTRADAY|PROBE symbol=... provider=EM|TX attempt=... queue=...ms dur=...ms http=... native=... tls=... bytes=.../... result=...
```

保留失败前后至少 30 秒日志，并结合同时期 `[sys]` 判断。

## 15. Wi-Fi 中断

加载有效股票和天气后断 Wi-Fi 至少 2 分钟：

- 当前 App 仍可操作
- 已有股票/天气缓存保留
- 菜单/DeviceInfo 仍响应
- UI 不等待 HTTP 卡死
- Wi-Fi 恢复后无需人工重启即可继续后续请求

## 16. 多 App 稳定性量化

至少：

```text
Stock -> Menu -> Weather -> Menu -> DeviceInfo -> Menu -> Stock 循环 >=100 次
button short-after-long = 0
watchdog = 0
unexpected reboot = 0
freeze = 0
stock cache loss = 0
weather cache loss = 0
monotonic heap leak = 0
```

建议同时混入股票短按切换，使输入状态机覆盖真实使用。

## 17. 股票交易时段量化

原有目标继续有效：

```text
股票切换 >=100 次
quote 请求 >=500 次
quote 成功率目标 >=99%（测试网络条件）
intraday 刷新周期 >=30 个
EastMoney->Tencent 组合分时周期成功率目标 >=80%
健康 quote Provider 下当前报价 P95 更新间隔 <=7 秒
单次 intraday 失败时 quote gap <=10 秒
intraday P95 age <=180 秒
watchdog = 0
unexpected reboot = 0
cache loss = 0
```

若组合分时 30+ 周期最终成功率仍 <80%，再独立评审其他 Provider；不要无限重试或放宽 parser。

## 18. 完整交易日

最终仍建议至少运行一次 **09:25–15:10**，期间穿插进入 Weather/Menu/DeviceInfo：

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
Commit SHA:
Board: LILYGO T-Display-S3
Port:
Wi-Fi:
Date:
Validators:
Ubuntu native:
Windows native:
Firmware build:
Flash:
Menu/Input:
DeviceInfo/IP:
Weather live:
Weather pet/UI:
Tencent intraday fallback:
Stock regression:
100-switch stability:
Heap/resource notes:
Full-day:
Notes:
```

没有实体板证据的功能必须保持 **PENDING**。
