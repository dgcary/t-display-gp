# T-Display GP

LILYGO T-Display-S3 320×170 多应用桌面终端。

```text
股票 / 天气 / Bambu Lab / 智能家居 / 设备信息
```

默认启动 Stock，没有自动 idle 跳转。

## Bambu Lab Cloud

T-Display 是 **只读 Cloud 监视器**。不依赖打印机 LAN MQTT，不提供 pause/resume/stop/温度/灯光/相机控制。配置与 Home Assistant 共用可信 LAN 页面：

```text
http://<T-Display-IP>:8081/
```

### Manual Token / printers

1. 浏览器正常登录对应区域的 Bambu 网站；
2. 从开发者工具 Cookies 复制 `token`；
3. 在 `:8081` 粘贴 Access Token；
4. 添加 1–4 台打印机的 `名称 + Serial`；
5. 选择当前打印机并保存。

Token 留空保存表示保留现有 Token。固件不会在状态 API/串口中回显 Token。“用 Token 获取我的打印机”只有用户主动点击才访问 Cloud，结果 Save 前不写 NVS。

设备端切换：

```text
GPIO0 short  -> 上一台
GPIO14 short -> 下一台
```

选择首尾循环并立即持久化。**配置中的每台打印机都有独立的常驻 MQTT/TLS slot 和状态缓存；A↔B 切换只切 active slot，不断开另一台、不重新 TLS/MQTT 登录、不等待 pushall。** 如果目标 slot 尚未在线，页面显示该 slot 当前缓存/连接状态，后台按自己的重连节奏恢复。

代码支持最多 4 个 slot；当前主要真机验收目标是用户实际的 2 台打印机同时在线。4 台同时在线需要单独做资源/稳定性验收后再视为已验证能力。

### Anti-flicker

Bambu 页面周期读取 service cache，但只有展示数据真实变化才 dirty。整屏清除只发生在首次进入或显式 full redraw；进度/温度/任务/耗材/footer 只局部重画，避免原先约 500 ms 一次的整屏闪烁。

### Cloud MQTT

```text
China:  cn.mqtt.bambulab.com:8883
Global: us.mqtt.bambulab.com:8883
username = cloudUserId
password = accessToken
每个 slot subscribe = device/<serial>/report
每个 slot request   = device/<serial>/request
```

只允许只读 `pushall` 状态同步 publish。每个 MQTT slot 使用 40960-byte buffer、30 s keepalive。rc 4/5 对对应 slot 进入 token-invalid 保护，不用同一凭据持续猛试。

#### BambuHelper-aligned persistent multi-printer runtime

当前运行时参考 `Keralots/BambuHelper`（MIT，迁移参考 commit `d7a898394c046495798d87e50afd91ecf63f6ce7`）：

```text
Arduino loop()
 -> BambuMqttService::process(nowMs)
 -> service all connected printer slots
 -> if needed, attempt at most one disconnected slot per loop pass
 -> per-slot fresh WiFiClientSecure, strict CA, timeout 15 s
 -> per-slot PubSubClient@2.8, buffer 40960, keepalive 30 s
 -> random bblp_* client ID
 -> mqtt.connect(cloudUserId, accessToken)
 -> subscribe device/<slot serial>/report
 -> >=2 s 后该 slot 发送一次 read-only pushall
```

关键约束：

- `espressif32@6.12.0` / Arduino-ESP32 2.0.17；
- 没有固定 CPU0 的 `bambu-mqtt` 专用任务；
- 每个已配置 printer slot 独立持有 TLS/MQTT、backoff、pushall 序号和 `BambuState`；
- 已在线 slot 常驻，active printer 切换不会 teardown 网络连接；
- callback 按 report topic/Serial 路由到对应 slot cache；
- 单个 slot 掉线只重建该 slot，其他在线 slot 保持；
- 重连 backoff = 30 s → 60 s（>=5 failures）→ 120 s（>=15 failures）；
- initial pushall 延迟 2 s；
- 允许像 BambuHelper 一样在已知长操作前 `esp_task_wdt_reset()`，但绝不关闭/删除 watchdog；
- strict CA，禁止 `setInsecure()`；
- 旧 `mqtt_real_tls_*` / layered preflight / 项目 `MQTT_SOCKET_TIMEOUT=3/5` 已退出生产路径。

Bambu 是设备级后台服务，离开 Bambu App 不断开。Stock/Weather/HA 的短连接仍通过 `NetworkArbiter` 与新 MQTT 建连事务串行；已经建立的持久 MQTT sockets 不长期占用 arbiter。

## Home Assistant

只读 REST client；1–4 entities，refresh 30–300 s。HTTP 仅可信 LAN；HTTPS 必须配置 CA，无 insecure fallback。

## Weather / Bad Apple

Open-Meteo；UI 显示 current + 今/明。Bad Apple：168×126 at x=152,y=27，2190 frames @ 10 FPS，silent loop，本地 flash。

## Stock / DeviceInfo

Stock：Tencent primary，EastMoney fallback；quote/intraday health 独立，失败保留 cache。

DeviceInfo：IP、SSID/RSSI/MAC、uptime/time、heap/min heap、PSRAM、Web 地址；不显示 secret。

## Architecture

```text
Stock -> dedicated MarketDataWorker
Weather + HomeAssistant -> one shared AppDataWorker
Bambu -> loop-driven persistent per-printer Cloud MQTT slots + local config
DeviceInfo -> local-only
Bad Apple -> local flash playback
```

## Build / Verification

```bash
python tools/validate_tdisplay_setup.py
python tools/validate_provisioning_contract.py
python tools/validate_http_transport_contract.py
python tools/validate_app_shell_contract.py
python tools/validate_dashboard_apps_contract.py
python tools/validate_bambu_cloud_contract.py
python tools/validate_bambu_pubsub_timeout_contract.py
python tools/validate_bad_apple_contract.py
pio test -e native
python tools/prepare_bad_apple_asset.py
pio run -e lilygo-t-display-s3
```

CI 同时跑 Windows native，并发布 exact-SHA artifact。普通升级只刷 exact-SHA `firmware.bin` 到 manifest offset（通常 `0x10000`），不 erase NVS，不改 bootloader/partition table。

详细契约：`docs/deployment.md`、`docs/api-contract.md`、`docs/hardware-acceptance.md`。
