# T-Display-S3 Hardware Acceptance

本清单只用于 **LILYGO T-Display-S3 真机**。Host tests / firmware build 不能代替实体板验收。

## 当前状态

- 自动化 native tests：由 GitHub Actions 验证
- ESP32-S3 firmware build：由 GitHub Actions 验证
- 实体 flash / 横屏 / 按键 / Wi-Fi / 实时行情稳定性：**PENDING，必须在真机完成**

每次真机验收记录：branch、40 位 commit SHA、日期、Wi-Fi 环境和结果。

## 1. Build / Flash

```bash
pio test -e native
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
pio run -e lilygo-t-display-s3
pio run -e lilygo-t-display-s3 -t upload --upload-port <PORT>
pio device monitor --port <PORT> -b 115200
```

要求：无启动循环、panic、watchdog、MarketDataWorker 启动失败。

## 2. 屏幕与按键

硬件不变量：

- GPIO15 屏幕供电 HIGH
- GPIO38 背光
- GPIO0 上一只
- GPIO14 下一只
- ST7789 物理 170×320
- 应用逻辑 **320×170 横屏**

通过标准：

- 横屏方向正确，无裁切/花屏
- 中文名称可读
- 正涨红、下跌绿
- 左侧价格/涨跌/开高低昨/量额布局可读
- 右侧分时图充分利用屏幕
- `昨收` 与 `今开` 参考线可区分；今开无效时不绘制
- 午休 11:30→13:00 不画假连续线
- 每次按键只切一只；长按不自动连跳
- 快速切股无明显 UI 卡死

## 3. 首次配网

擦除/无有效配置后：

1. `TDisplay-GP-Setup` 可见
2. 手机可进入 Captive Portal
3. 可保存 Wi-Fi + 3–5 股票 + 3/4/5 秒刷新
4. 保存后受控重启
5. 重启后自动连 Wi-Fi
6. 串口最终出现：

```text
[boot] market loop ready
```

## 4. 行情基本功能

建议至少测试 SSE、SZSE、BSE 示例。

验证：

- 当前价和指标合理
- 报价按配置周期持续更新
- 分时独立刷新
- EastMoney 正常时 footer 为 EM
- EastMoney quote 连续故障后腾讯 quote fallback 能工作
- 分时仍保持 EastMoney-only，不擅自切腾讯趋势

BSE 腾讯备用若真机不符合现有 schema，记录为限制，不猜字段/前缀。

## 5. `[md]` 请求日志

行情运行时必须能看到类似：

```text
[md] id=... type=QUOTE|INTRADAY|PROBE symbol=... provider=EM|TX attempt=... queue=...ms dur=...ms http=... native=... tls=... bytes=.../... result=...
```

发生失败时记录实际设备错误，不把 Windows Schannel/curl 错误直接等同为 ESP32 错误。

## 6. 分时失败恢复

在 EastMoney trends2 波动时重点验证：

- 单次分时失败不清空旧图
- 单次分时失败不把正常报价标成全局“数据异常”
- 分时 transient failure 最多 3 次尝试
- 重试之间报价仍有机会优先执行
- 新股票的 pending 分时可替换已过时、尚未执行的旧分时
- 分时持续过旧时显示 `分时延迟`
- 后续分时成功后自动恢复正常状态

## 7. Quote / Intraday 状态隔离

验证：

- quote 失败不会清空 intraday cache
- intraday 成功不会清除 quote 错误
- intraday 失败不会污染 quote health
- quote 成功不会错误清掉 intraday health
- quote age >=15 秒时可显示 `报价延迟`
- intraday age >=180 秒时可显示 `分时延迟`

## 8. Wi-Fi 中断

加载有效画面后断 Wi-Fi 至少 2 分钟：

- 显示 `离线`
- 已有报价/分时仍可查看
- 按键仍响应
- UI 不等待 HTTP 卡死
- Wi-Fi 恢复后无需人工重启即可恢复请求

## 9. 稳定性量化验收

交易时段使用 5 只股票：

```text
股票切换 >=100 次
quote 请求 >=500 次
quote 成功率目标 >=99%（测试网络条件）
intraday 刷新周期 >=30 个
intraday 有限重试后周期成功率目标 >=80%
健康 quote Provider 下当前报价 P95 更新间隔 <=7 秒
单次 intraday 失败时 quote gap <=10 秒
intraday P95 age <=180 秒
watchdog = 0
unexpected reboot = 0
cache loss = 0
```

如果 30+ 分时周期最终成功率仍 <80%，进入分时备用 Provider 的独立设计评审；不要无限重试或放宽 parser。

## 10. 完整交易日

最终目标：至少运行一次 **09:25–15:10**：

- 开盘前
- 上午盘
- 午休
- 下午盘
- 收盘

要求全程无异常重启/卡死/缓存丢失，收盘后保留最终报价与分时图。

## Acceptance record

建议在 PR/Issue 或部署记录中写：

```text
Repository: dgcary/t-display-gp
Branch:
Commit SHA:
Board: LILYGO T-Display-S3
Port:
Wi-Fi:
Date:
Host tests:
Firmware build:
Flash:
Smoke:
Stability:
Full-day:
Notes:
```

没有实体板证据的项目必须保持 PENDING。
