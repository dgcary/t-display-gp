# T-Display GP

LILYGO T-Display-S3 320×170 多应用桌面终端。

当前应用：

```text
股票 / 天气 / Bambu Lab / 智能家居 / 设备信息
```

默认启动 **Stock**，没有自动 idle 跳转；页面保持到用户主动切换。

## Bambu Lab Cloud

T-Display 是 **只读 Cloud 监视器**。不依赖打印机 LAN MQTT，不提供 pause/resume/stop/温度/灯光/相机控制。

配置入口与 Home Assistant 共用：

```text
http://<T-Display-IP>:8081/
```

Bambu 现在采用 **Manual Token** 主流程：

1. 在浏览器正常登录对应区域的 Bambu 网站；
2. 从浏览器开发者工具 Cookies 中复制 `token`；
3. 在 `:8081` 的 Bambu 区域粘贴 Access Token；
4. 添加 1–4 台打印机的 `名称 + Serial`；
5. 选择“当前打印机”；
6. 点“保存并切换”。

Access Token 留空保存表示保留当前 Token，固件永不从状态 API/串口回显 Token。Token 等价于登录凭据，不要发到聊天、GitHub、截图或日志中。

页面提供可选的 **“用 Token 获取我的打印机”**。只有用户主动点击时才请求 Bambu HTTPS device list；结果只填入当前网页表单，不自动覆盖 NVS。日常打印机切换完全使用本地保存的列表，不调用 Cloud discovery。

本地最多保存 4 台打印机。切换 active printer 不需要重启、不需要重新登录、不需要验证码；`BambuMqttService` 会断开旧 MQTT，清空旧打印机状态快照，并以新 Serial 重新连接/订阅，防止两台设备状态串台。

Bambu App 本机也可以直接切换已经保存的打印机：GPIO0 短按切上一台，GPIO14 短按切下一台，首尾循环；少于两台时不动作。选择会立即写回 Bambu NVS，清空旧状态并触发新 Serial 的 MQTT 重连。GPIO0 长按回菜单、GPIO14 长按无操作的全局语义不变。

Bambu 页面采用差量呈现：后台仍以 500 ms 周期读取 service cache，但只有展示状态实际变化时才标记 dirty。整屏清除只发生在首次进入页面或主动切换打印机的 full redraw；实时进度、温度、任务、耗材和 footer 只擦除并重画对应区域，避免周期性整屏闪烁。

配置 schema 为 v2：

```text
enabled
region
accessToken
cloudUserId
printers[0..3] { serial, name }
printerCount
activePrinterIndex
```

旧 schema v1 会迁移已有 Token / Cloud User ID / 单台打印机；旧账号/密码不再进入新配置。

Cloud User ID 优先从 JWT Token 本地解析。只有手动 discovery 在需要时才使用短生命周期 HTTPS；Bambu HTTPS/MQTT 均保持严格 CA 校验，禁止 `setInsecure()`。

Broker：

```text
China: cn.mqtt.bambulab.com:8883
US/EU/Global: us.mqtt.bambulab.com:8883
```

MQTT：

```text
username  = cloudUserId
password  = accessToken
subscribe = device/<activeSerial>/report
request   = device/<activeSerial>/request
```

只允许一次只读 `pushall` 状态同步 publish；禁止任何打印机控制 publish。MQTT receive buffer = 40960 bytes。

Token 被 MQTT 以 rc 4/5 拒绝后进入 `token_invalid`，固件不会用旧 Token/旧密码自动撞 Cloud，也没有 SMS/Email/TFA 登录状态机。用户重新从浏览器取得新 Token，在 `:8081` 粘贴保存即可恢复。

Bambu MQTT 是设备级后台服务，离开 Bambu App 不断开；Stock/Weather/HA 仍可使用各自网络请求。连接/掉线诊断只输出非敏感字段：MQTT rc、TLS numeric error、Wi-Fi status/RSSI 和 free heap，不输出 Token、Cloud User ID 或认证载荷。当前重连/keepalive 策略不会仅因诊断日志而改变，真实网络稳定性仍以真机证据为准。

## Home Assistant

T-Display 只是用户现有 Home Assistant 的只读 REST client：

```text
GET <base_url>/api/states/<entity_id>
Authorization: Bearer <Long-Lived Access Token>
```

1–4 entities，refresh 30–300 s。HTTP 仅适合可信 LAN；HTTPS 必须配置 CA，禁止 insecure fallback。

## Weather / Bad Apple

Open-Meteo；UI 显示 current + 今/明。Bad Apple：

```text
viewport 168×126 at x=152,y=27
2190 frames @ 10 FPS ≈ 219 s
1-bit, silent, loop, local flash
```

无顶部横分隔线/视频左竖分隔线；离开 Weather 停止刷新，重新进入从 frame 0 开始。

## Stock / DeviceInfo

Stock：Tencent primary，EastMoney fallback；quote/intraday health 独立，失败保留 cache。

DeviceInfo：IP、SSID/RSSI/MAC、uptime/time、heap/min heap、PSRAM、Web 地址；不显示 secret。

## Architecture

```text
Stock -> dedicated MarketDataWorker
Weather + HomeAssistant -> one shared AppDataWorker
Bambu -> persistent Cloud MQTT service + local multi-printer config
DeviceInfo -> local-only
Bad Apple -> local flash playback
```

短生命周期 HTTP/TLS 与 MQTT connect/reconnect handshake 经过 `NetworkArbiter`；已建立的持久 MQTT socket 不长期占用 arbiter。

## Build / Verification

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_app_shell_contract.py
python tools/validate_dashboard_apps_contract.py
python tools/validate_bambu_cloud_contract.py
python tools/validate_bad_apple_contract.py
pio test -e native
python tools/prepare_bad_apple_asset.py
pio run -e lilygo-t-display-s3
```

CI 还跑 Windows native，并发布 exact-SHA artifact：`firmware.bin`、`partitions.bin`、`bootloader.bin`、`firmware-manifest.txt`。

普通升级只刷 exact-SHA `firmware.bin` 到 manifest offset（通常 `0x10000`），不 erase NVS，不改 bootloader/partition table。

详细契约：`docs/deployment.md`、`docs/api-contract.md`、`docs/hardware-acceptance.md`。