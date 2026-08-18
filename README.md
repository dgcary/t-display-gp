# T-Display GP

基于 **LILYGO T-Display-S3** 的 A 股实时行情桌面终端。

> **GitHub `dgcary/t-display-gp` 是项目唯一事实源。** 开发、修复和 Codex 部署都应从 GitHub 指定分支/提交开始。

## V1 功能

- 沪/深/北 A 股，股票池 3–5 只
- 报价 3–5 秒刷新可配置
- 东方财富：报价 + 分时主源
- 腾讯：报价备用源
- **320×170 横屏界面**
- 实体按键切换股票
- 首次 Captive Portal 配网 + 局域网 Web 配置
- 网络/Provider 失败时保留最后有效报价和分时图
- 分时午休断点
- 分时图同时显示 **昨收** 和 **今开** 参考线
- 报价与分时健康状态独立，不再因一次分时失败把整页标成“数据异常”

## 行情稳定性设计

行情 HTTP 始终在 `MarketDataWorker` 中执行，主循环不做阻塞网络请求。

请求优先级：

```text
当前股票报价 > 后台报价 > 主源恢复探测 > 分时 > 分时重试
```

分时采用 **latest-wins**：尚未执行的旧分时请求不会在快速切股时不断堆积。

东财分时发生可恢复的网络/服务器错误时，最多 3 次尝试：

```text
首次失败 -> 约 1.5s ±20% 后重试
再次失败 -> 约 4s ±20% 后重试
仍失败 -> 本轮结束，继续保留旧分时图
```

重试是延迟任务，必须让出报价请求；不会在 Provider 内连续阻塞重试。

页面状态：

- Wi-Fi 断开：`离线`
- 尚无有效报价：`等待报价`
- 报价超过 15 秒未成功更新：`报价延迟`
- 分时超过 180 秒未成功更新：`分时延迟`
- 单次偶发分时失败且旧图仍新鲜：不显示全局错误

## 请求诊断日志

串口 `115200` 会输出一行一个行情请求的 `[md]` 日志，例如：

```text
[md] id=182 type=INTRADAY symbol=000831 provider=EM attempt=2/3 queue=8ms dur=2680ms http=200 native=-5 tls=-29184 bytes=8192/13824 result=NETWORK
```

可直接区分：

- 哪只股票 / 哪类请求
- EastMoney / Tencent
- 第几次尝试
- 排队时间和请求耗时
- HTTP 状态
- HTTPClient 原生错误
- TLS 错误
- 实收/期望响应大小

不要把 Windows `curl`/Schannel 错误直接等同于 ESP32 错误，应以设备 `[md]` 日志为准。

## 横屏界面

物理屏幕仍是 ST7789 170×320，固件旋转为 **320×170 横屏**：

```text
┌──────────────┬────────────────────────┐
│ 股票 / 价格   │                        │
│ 涨跌 / 指标   │       分 时 图          │
│ 开高低昨      │   昨收 ----            │
│ 量 / 额       │   今开 · · ·           │
├──────────────┴────────────────────────┤
│ 页码 / WiFi / EM|TX / 状态             │
└───────────────────────────────────────┘
```

`昨收` 和 `今开` 使用不同线型，不只依赖颜色区分。今开无有效数据时不猜测、不绘制。

## 硬件

- LILYGO T-Display-S3
- ESP32-S3
- ST7789 170×320，8-bit parallel
- GPIO15：屏幕供电
- GPIO38：背光
- GPIO0：上一只
- GPIO14：下一只
- TFT RGB 顺序：`TFT_RGB`
- `INIT_SEQUENCE_3`

## 首次使用

无有效配置时：

1. 连接热点 `TDisplay-GP-Setup`
2. 打开 Captive Portal（必要时访问 `192.168.4.1`）
3. 选择 Wi-Fi
4. 填入 3–5 个 A 股代码
5. 选择 3 / 4 / 5 秒报价刷新周期
6. 保存
7. 设备受控重启后自动连接 Wi-Fi 并进入行情界面

示例代码：`600519`、`300750.SZ`、`920047.BJ`。代码与后缀冲突会被拒绝。

## 开发与烧录

```bash
pio test -e native
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
pio run -e lilygo-t-display-s3
pio run -e lilygo-t-display-s3 -t upload
pio device monitor -b 115200
```

测试或 firmware build 失败时不得烧录。

完整步骤见 [docs/deployment.md](docs/deployment.md)。

## 目录

```text
AGENTS.md              Codex / 自动化 Agent 规则
include/               固件常量
lib/core/              股票代码、配置、交易时钟、故障切换
lib/providers/         Provider 接口与解析器
src/app/               StockController
src/network/           HTTP / MarketDataWorker / 配网
src/device/            T-Display-S3 硬件层
src/ui/                横屏 UI / 分时图
test/                  PlatformIO native tests
docs/                  API、部署、真机验收、设计规格
```

## 核心原则

1. 主循环不做行情 HTTP。
2. Quote/Intraday 通过 Provider 抽象访问，不把原始接口格式写进 UI。
3. 缓存优先：网络失败不清空最后有效画面。
4. Parser 严格失败关闭，不为了“提高成功率”接受异常数据。
5. 不通过无限重试、无限延长 timeout 或降低 TLS 安全性掩盖问题。
6. 真机 PASS 必须来自实体 T-Display-S3，不以 host test 或 firmware build 代替。

## Provider 说明

EastMoney/Tencent 使用的是公开但非官方稳定契约的接口，可能发生格式或访问策略变化。详细字段、错误和故障切换约定见 [docs/api-contract.md](docs/api-contract.md)。

当前稳定性改造**不新增分时备用 Provider**。如果真机在有限重试后，30 个以上分时刷新周期成功率仍低于 80%，再单独评审分时备用源。

## 真机稳定性目标

最终验收至少包括：

- 5 只股票
- 100+ 次按键切换
- 500+ 次报价请求，测试网络下目标成功率 ≥99%
- 30+ 个分时刷新周期，有限重试后目标成功率 ≥80%
- 健康报价源下当前报价 P95 更新间隔 ≤7 秒
- 单次分时失败不应造成 >10 秒报价空档
- 分时 P95 数据年龄 ≤180 秒
- 无 watchdog、异常重启、缓存丢失
- 最终完成一次 09:25–15:10 完整交易日运行

详见 [docs/hardware-acceptance.md](docs/hardware-acceptance.md)。

## TLS 说明

当前 V1 延续既有 `WiFiClientSecure::setInsecure()` 行为；本次行情稳定性修改不进一步降低 TLS 安全性，也不把 TLS 加固与稳定性修复混在同一批改动中。若未来加入账户、Token、交易或其他敏感数据，必须单独恢复严格证书验证。
