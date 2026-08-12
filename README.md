# T-Display GP

基于 **LILYGO T-Display-S3** 的 A 股实时行情桌面终端。

本项目面向 ESP32-S3 + 170×320 ST7789 彩屏，目标是在不依赖手机 App、PC 常驻程序或额外服务器的情况下，通过 Wi-Fi 直接获取 A 股公开行情，在设备上显示当前价、涨跌、日内指标和分时走势图。

> **GitHub 仓库 `dgcary/t-display-gp` 是本项目唯一事实源（Source of Truth）。**
> 后续开发、修复、部署和 Codex 操作都应从 GitHub 最新代码开始，不以本地旧副本为准。

## V1 功能

- A 股：沪市 / 深市 / 北交所代码识别
- 股票池：3–5 只
- 交易时段快照刷新：3–5 秒可配置
- 分时图刷新：60 秒
- 两个实体按键切换上一只 / 下一只股票
- 首次启动自动进入 Wi-Fi Captive Portal 配网
- 手机浏览器修改股票池和刷新周期
- 东方财富作为主行情源
- 腾讯行情作为快照备用源
- 主行情源连续失败后自动切换，恢复后自动切回
- 非交易时段保留最后行情和当日分时
- Wi-Fi/行情异常时保留缓存，不阻塞按键和 UI
- 170×320 竖屏界面，支持中文股票名称
- 分时图午休断点，不把 11:30→13:00 画成连续交易

## 硬件

目标设备：**LILYGO T-Display-S3**

- MCU：ESP32-S3
- 屏幕：ST7789，170×320
- Flash：16 MB（PlatformIO 官方板卡定义）
- 显示供电：GPIO15，初始化 TFT 前必须拉高
- LCD Backlight：GPIO38
- 上一只股票：GPIO0
- 下一只股票：GPIO14

TFT 参数与 TFT_eSPI 官方 `Setup206_LilyGo_T_Display_S3.h` 对齐，使用 RGB 顺序、`INIT_SEQUENCE_3` 和 8-bit parallel bus。

## 使用方式

### 第一次开机

如果设备没有有效 Wi-Fi/股票配置：

1. 设备启动热点：`TDisplay-GP-Setup`
2. 手机连接该热点
3. 系统通常会自动弹出 Captive Portal；没有弹出时访问 `192.168.4.1`
4. 选择 Wi-Fi
5. 填入 3–5 个 A 股代码，例如：
   - `600519` 或 `600519.SH`
   - `300750` 或 `300750.SZ`
   - `920047` 或 `920047.BJ`
6. 可填写自定义股票名称
7. 选择 3 / 4 / 5 秒刷新间隔
8. 保存后设备连接 Wi-Fi 并进入股票界面

股票代码与交易所后缀必须一致，例如 `600519.SZ` 会被配置校验直接拒绝。

### 正常运行后修改配置

在同一局域网中，用浏览器访问设备 IP：

- 修改股票池
- 修改快照刷新间隔
- 查看当前 IP、RSSI 和运行时间
- 触发“更换 Wi-Fi”并重新进入配网模式

配置保存采用“**先写入 NVS，再受控重启**”方式，避免运行中的 Controller/UI 出现半更新状态。

## 行情架构

```text
                    T-Display GP
                         │
                    StockController
                    /             \
             MarketDataWorker      StockScreen
                    │
             Provider interface
              /             \
       EastMoney          Tencent
       primary            fallback
          │
     snapshot + trend
```

### Provider 策略

- **东方财富**：主快照 + 主分时
- **腾讯**：快照备用
- 东方财富在 60 秒窗口内连续失败 3 次后，快照切换腾讯
- 主源恢复探测不快于 120 秒一次
- 连续 2 次恢复成功后切回东方财富
- 分时数据 V1 始终由东方财富提供，主源异常时保留旧分时缓存

详细字段与超时规范见：[docs/api-contract.md](docs/api-contract.md)。

## 交易时段行为

设备按中国标准时间运行：

- 09:30–11:30：连续交易
- 11:30–13:00：午休
- 13:00–15:00：连续交易
- 收盘后：显示已收盘并保留最后行情/分时
- 次交易日开盘：自动恢复高频刷新
- 周末/节假日：不进行 3–5 秒高频轮询

Controller 会区分“昨天缓存”与“今天已确认休市”，避免设备隔夜运行后被旧时间戳错误阻塞第二天开盘刷新。

## 开发环境

推荐：**PlatformIO + Arduino/C++**。

```bash
pio test -e native
pio run -e lilygo-t-display-s3
```

### 烧录

```bash
pio run -e lilygo-t-display-s3 -t upload
pio device monitor -b 115200
```

完整部署和真机检查步骤见：[docs/deployment.md](docs/deployment.md)。

## 目录结构

```text
.
├── AGENTS.md                    # 给 Codex / 自动化 Agent 的操作约束
├── platformio.ini               # PlatformIO 固件与 native test 环境
├── include/
│   └── build_config.h
├── lib/
│   ├── core/                    # 股票代码、配置、交易时钟、格式化、故障切换
│   └── providers/               # Provider 接口与纯 C++ JSON/文本解析器
├── src/
│   ├── app/                     # StockController
│   ├── device/                  # NVS、按钮、T-Display-S3 硬件层
│   ├── network/                 # HTTP、Worker、配网与 Web 配置
│   ├── ui/                      # 170×320 股票界面和分时图
│   └── main.cpp                 # 固件启动与非阻塞主循环
├── test/                        # PlatformIO native / host 行为测试
├── tools/                       # 构建契约校验
└── docs/
    ├── api-contract.md          # 行情接口字段与故障切换契约
    ├── deployment.md            # Codex/人工烧录与验收步骤
    ├── hardware-acceptance.md   # 真机详细验收清单
    └── superpowers/             # 已批准设计规格与实现计划
```

## 设计原则

1. **UI loop 不做 HTTP。** 网络请求在 FreeRTOS MarketDataWorker 中执行，主循环只做队列、按键、WebServer 和局部 UI 更新。
2. **行情接口可替换。** Provider 和 Parser 分层，避免将某个非正式公开接口写死在 UI/Controller。
3. **缓存优先于黑屏。** 网络错误时保留最后有效行情和分时。
4. **配置原子化。** 手机配置先持久化，再重启整套模块。
5. **真实硬件证据优先。** Host test 通过不等于真机通过；没有实际 build/flash/屏幕验证时不得把 PENDING 写成 PASS。
6. **尽量复用成熟生态。** 使用 TFT_eSPI、U8g2_for_TFT_eSPI、WiFiManager、ArduinoJson，不重新造底层轮子。

## 测试状态

当前开发分支的软件侧自动化验证包含：

- 股票代码规范化与市场映射
- 配置编码/解码和校验
- A 股交易时段状态机
- 东方财富 parser
- 腾讯 parser
- Provider URL/HTTP 合约与故障切换
- Wi-Fi/股票配置表单核心
- 按键消抖/长按/`millis()` 回绕
- Worker 请求去重
- Controller 缓存、切股、刷新和故障转移
- 分时图坐标和 UTF-8 安全截断
- 跨交易日/旧缓存边界

开发环境最终 host 回归：**58/58 tests passing**。

### 仍需真机完成的项目

以下项目在没有实体 T-Display-S3 和完整 PlatformIO 网络环境时不能声称通过：

- 真实 ESP32-S3 PlatformIO firmware build
- USB 烧录
- LCD 颜色、方向和中文字体实显
- GPIO0/GPIO14 实体按键
- Captive Portal 手机实测
- 东方财富/腾讯在设备网络上的实时访问
- 长时间运行/隔夜切换

请严格按照 [docs/hardware-acceptance.md](docs/hardware-acceptance.md) 记录结果。

## 安全说明

V1 的公开行情 HTTPS 传输可能使用 `WiFiClientSecure::setInsecure()`。请求不包含账户、密码、交易凭证或其他私密 payload，但这意味着设备不会验证远端 TLS 身份。

如果后续项目加入券商账户、交易接口、Token 或任何敏感数据，**必须先改为证书验证，不能沿用当前 transport identity trade-off**。

## Codex / 自动化 Agent

如果使用 Codex 直接从 GitHub 部署到设备：

1. 必须先阅读根目录 [AGENTS.md](AGENTS.md)
2. 默认以 GitHub 最新目标分支为准，不复用本机旧工程
3. 先运行 native tests
4. 再执行 firmware build
5. build 成功后才允许 upload
6. 烧录后必须执行最低真机 smoke test
7. 不得因为 host test 通过就声称硬件验收通过

详细命令见 [docs/deployment.md](docs/deployment.md)。
