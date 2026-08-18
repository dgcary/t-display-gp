# T-Display GP 部署与烧录指南

本文件用于人工或 Codex 从 GitHub 代码直接编译并部署到 **LILYGO T-Display-S3**。

## 1. 部署原则

- GitHub `dgcary/t-display-gp` 是唯一事实源。
- 每次烧录必须知道准确的 **branch + commit SHA**。
- 默认部署已批准的 `main`。
- 如果用户指定 feature branch/commit，则只能部署指定 ref。
- 不允许用本机之前遗留的 `.pio`、旧 firmware.bin 或另一份工程目录代替 GitHub 当前源码。

建议在部署记录中保存：

```text
Repository: dgcary/t-display-gp
Branch: <branch>
Commit: <40-char SHA>
Board: LILYGO T-Display-S3
Date: <local time>
Result: BUILD / FLASH / SMOKE / ACCEPTANCE
```

## 2. 环境要求

需要：

- Git
- Python 3
- PlatformIO Core (`pio`)
- USB 数据线
- 可识别 ESP32-S3 的 USB 端口
- 能访问 PlatformIO library registry / GitHub dependencies 的网络

确认：

```bash
git --version
pio --version
pio device list
```

## 3. 获取 GitHub 最新代码

首次：

```bash
git clone https://github.com/dgcary/t-display-gp.git
cd t-display-gp
git checkout main
git pull --ff-only origin main
```

已经存在 checkout 时：

```bash
cd t-display-gp
git status --short
git fetch origin
git checkout main
git pull --ff-only origin main
git rev-parse HEAD
```

如果工作区有未提交改动，不要直接 reset/drop。先确认这些改动是否属于用户需要保留的内容。

部署指定 feature branch：

```bash
git fetch origin
git checkout <branch>
git pull --ff-only origin <branch>
git rev-parse HEAD
```

## 4. 部署前测试

必须先跑 native tests：

```bash
pio test -e native
```

任何失败都先停止，不要继续烧录。

如果修改过 TFT 相关 build flags，再执行：

```bash
python tools/validate_tdisplay_setup.py
```

## 5. 固件构建

```bash
pio run -e lilygo-t-display-s3
```

预期产物位于 PlatformIO build 目录（通常 `.pio/build/lilygo-t-display-s3/`）。

只有 firmware build 成功后才能进入 upload。

## 6. 连接设备

连接 LILYGO T-Display-S3 后：

```bash
pio device list
```

如果有多个串口，明确选择目标设备，不要猜测上传端口。

必要时可在命令中指定：

```bash
pio run -e lilygo-t-display-s3 -t upload --upload-port <PORT>
```

Windows 示例：

```text
COM7
```

Linux 示例：

```text
/dev/ttyACM0
```

## 7. 烧录

自动识别单个设备时：

```bash
pio run -e lilygo-t-display-s3 -t upload
```

烧录后打开串口：

```bash
pio device monitor -b 115200
```

如果设备之前跑过其他固件，首次验收建议先擦除 Flash/NVS，再重新 upload，以验证完整首次配网流程。

可用 PlatformIO/esptool 进行 erase，但必须先确认目标串口确实是本项目的 T-Display-S3。

## 8. 第一次启动

没有有效配置时：

1. 等待热点 `TDisplay-GP-Setup`
2. 手机连接该热点
3. Captive Portal 自动打开；否则访问 `192.168.4.1`
4. 选择 Wi-Fi
5. 输入 3–5 只股票
6. 保存
7. 设备成功保存 Wi-Fi 和股票配置后会执行一次受控重启
8. 重启后使用已保存 Wi-Fi 自动连接，并进入主行情页面

推荐首轮：

```text
600519   贵州茅台
300750   宁德时代
920047   诺思兰德
```

其中北交所腾讯备用源仍需要在实际设备网络验证；如果 Tencent+BSE 不符合既有 schema，不得擅自猜测新字段/前缀，应按 `docs/hardware-acceptance.md` 记录为限制。

### 首次配网串口日志

配网阶段建议保持串口监视器开启：

```bash
pio device monitor -b 115200
```

正常首次配网应能看到类似阶段日志：

```text
[boot] provisioning start
[prov] entering startConfigPortal
[prov] portal started: ssid=TDisplay-GP-Setup ...
[prov] custom parameters submitted
[prov] portal returned: ... wifi_status=3 ip=...
[prov] application config saved
[prov] provisioning saved successfully; rebooting ...

# 自动重启后
[boot] provisioning start
[prov] entering autoConnect
[prov] connected using saved credentials: ip=...
[boot] provisioning complete; starting application services
[boot] market loop ready
```

如果已经拿到 IP 但没有看到 `portal returned`，说明仍卡在 WiFiManager 配网门户内部；保存本段串口日志，不要继续盲目烧录或修改行情逻辑。

## 9. 最低 Smoke Test

每次烧录至少确认：

- 屏幕亮起，无明显花屏
- 方向为 170×320 竖屏
- 正涨颜色为红色
- 中文股票名正常
- 当前价/涨跌/今开/高/低/昨收能显示
- 分时图未覆盖 footer
- GPIO0/GPIO14 每按一次只切一只
- 手机配置页可访问
- Wi-Fi 暂时断开时不会卡死 UI

第一次硬件版本或涉及硬件/UI/网络修改时，执行完整：

[hardware-acceptance.md](hardware-acceptance.md)

## 10. 验收状态规则

必须区分：

```text
HOST TEST PASS
FIRMWARE BUILD PASS
FLASH PASS
SMOKE PASS
HARDWARE ACCEPTANCE PASS
```

前一级通过不自动代表后一级通过。

特别禁止：

- 只跑 `pio test` 就写“设备验证通过”
- 只 build 没 upload 就写“部署成功”
- upload 成功但没看屏幕/按键就写“真机验收通过”

## 11. 常见问题

### 找不到 `pio`

安装 PlatformIO Core，然后重新执行：

```bash
pio --version
```

### 找不到串口

- 换确认支持数据传输的 USB 线
- 检查 USB 驱动/权限
- `pio device list`
- 确认设备供电和 USB 枚举

### 屏幕颜色异常（红变蓝）

先确认 `platformio.ini` 仍为：

```text
TFT_RGB_ORDER=TFT_RGB
```

并运行：

```bash
python tools/validate_tdisplay_setup.py
```

不要用 BGR workaround 掩盖板型或 TFT 配置不匹配。

### 屏幕不亮

优先检查：

- 当前确实是 LILYGO T-Display-S3
- GPIO15 在 TFT init 前已 HIGH
- GPIO38 backlight 配置
- TFT_eSPI Setup206 对应参数是否仍一致

### 行情接口失败

先抓取 HTTP status/body，再查 `docs/api-contract.md`。不要直接放宽 JSON/parser 校验。

如果公开接口发生结构变化，应：

1. 保存新的真实 fixture
2. 写失败测试
3. 修改对应 Provider/Parser
4. 跑完整 native suite
5. build
6. 再刷真机验证
