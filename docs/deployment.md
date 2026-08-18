# T-Display GP 部署与烧录指南

用于人工或 Codex 从 GitHub 部署到 **LILYGO T-Display-S3**。

## 1. 唯一准本

- Repository: `dgcary/t-display-gp`
- 默认部署已批准的 `main`
- 如用户指定 branch/SHA，则严格部署指定 ref
- 每次真机验收记录准确 commit SHA

不要使用本机旧 `.pio`、旧 `firmware.bin` 或旧工程目录代替 GitHub 当前源码。

## 2. 获取代码

```bash
git fetch origin
git checkout <branch>
git pull --ff-only origin <branch>
git rev-parse HEAD
```

工作区有未提交内容时先停止确认，不要直接 reset/drop。

## 3. 部署前强制检查

```bash
pio test -e native
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
pio run -e lilygo-t-display-s3
```

任一失败：**禁止烧录**。

HTTP transport 合约检查会确认：

- connect timeout = 1500 ms
- HTTP/read timeout setting = 2500 ms
- TLS handshake timeout = 5 s
- `HTTPClient::setReuse(false)` 保持启用
- 不把毫秒常量直接传给 Arduino-ESP32 2.0.14 的秒制 `WiFiClientSecure::setTimeout()`

## 4. 烧录

查看串口：

```bash
pio device list
```

Windows 示例：

```bash
pio run -e lilygo-t-display-s3 -t upload --upload-port COM6
pio device monitor --port COM6 -b 115200
```

只有确认目标串口就是本项目 T-Display-S3 后才能执行 erase/upload。

## 5. 首次配网

无有效配置时：

1. 连接 `TDisplay-GP-Setup`
2. 打开 Captive Portal；必要时访问 `192.168.4.1`
3. 选择 Wi-Fi
4. 输入 3–5 只股票
5. 设置 3/4/5 秒报价周期
6. 保存
7. 设备受控重启
8. 重启后使用已保存 Wi-Fi 进入行情页

正常串口阶段：

```text
[boot] provisioning start
[prov] entering startConfigPortal / autoConnect
...
[boot] provisioning complete; starting application services
[boot] market loop ready
```

如果已拿到 IP 但没有进入 `market loop ready`，保留完整 `[prov]` 日志排查，不要改行情代码碰运气。

## 6. 新版行情请求日志

进入行情循环后重点观察 `[md]`：

```text
[md] id=... type=QUOTE|INTRADAY|PROBE symbol=... provider=EM|TX attempt=... queue=...ms dur=...ms http=... native=... tls=... bytes=.../... result=...
```

发生分时失败时，保存失败前后至少 30 秒日志。不要把 PC 的 Schannel/curl 错误直接当成 ESP32 错误；以设备 `native/tls/http/bytes` 为准。

## 7. 最低 Smoke Test

烧录后至少确认：

- 屏幕为 **320×170 横屏**，无裁切/花屏
- 正涨红、下跌绿
- 中文股票名正常
- 当前价、涨跌、开/高/低/昨、量/额可读
- 右侧分时图正常
- `昨收` 与 `今开` 两条参考线可区分
- 午休不连成假连续线
- GPIO0/GPIO14 每按一次只切一只股票
- 分时请求失败/重试期间按键和 UI 仍响应
- 分时失败时旧图保留，报价仍可继续更新
- 午休/收盘后不误报 `报价延迟` / `分时延迟`
- 串口无 watchdog / panic / reboot

## 8. 行情稳定性真机检查

建议配置 5 只股票，在交易时段收集 `[md]` 日志。

最低目标：

```text
100+ 次股票切换
500+ 次 quote 请求，测试网络目标成功率 >=99%
30+ 个 intraday 刷新周期，有限重试后目标成功率 >=80%
健康 quote 源下当前报价 P95 更新间隔 <=7 秒
单次 intraday 失败造成的 quote gap <=10 秒
intraday P95 age <=180 秒
unexpected reboot / watchdog / cache loss = 0
```

如果分时 30+ 周期后仍低于 80%，记录证据并进入“分时备用 Provider”单独设计，不要放宽 parser 或无限重试。

## 9. 状态判定

必须分别记录：

```text
HOST TEST PASS
FIRMWARE BUILD PASS
FLASH PASS
SMOKE PASS
STABILITY PASS
FULL HARDWARE ACCEPTANCE PASS
```

前一级不自动代表后一级。

## 10. 常见问题

### 方向仍是竖屏

确认烧录的是包含 landscape UI 的目标 commit，并检查 `DeviceLayer.cpp` 的目标逻辑尺寸应为 320×170。不要修改 TFT 物理 width/height build flags 来伪造横屏。

### 红色显示成蓝色

确认：

```text
TFT_RGB_ORDER=TFT_RGB
```

并运行 `python tools/validate_tdisplay_setup.py`。

### 行情/分时不稳定

优先看 `[md]` 的：

```text
http
native
tls
bytes
queue
dur
attempt
```

不要先拉长 timeout、关闭更多 TLS 检查或放宽 parser。当前 transport 已显式将 TLS handshake 限制为 5 秒；如果仍出现明显长于此值的单请求阻塞，保存完整 `[md]` 和前后串口日志再定位。

### 分时失败但报价正常

这是允许的降级状态。旧分时图应保留；单次失败通常后台有限重试，不应马上出现全局错误。持续过旧才显示 `分时延迟`。

完整真机验收见 [hardware-acceptance.md](hardware-acceptance.md)。
