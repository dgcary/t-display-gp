# T-Display GP

LILYGO T-Display-S3 320×170 多应用桌面终端。

```text
股票 / 天气 / Bambu Lab / 智能家居 / 设备信息
```

默认启动 Stock，没有自动 idle 跳转。

## Bambu Lab Cloud

T-Display 是 **只读 Cloud 监视器**。不依赖打印机 LAN MQTT，不提供 pause/resume/stop/温度/灯光/相机控制。

配置入口与 Home Assistant 共用：

```text
http://<T-Display-IP>:8081/
```

### Manual Token

1. 在浏览器正常登录对应区域的 Bambu 网站；
2. 从浏览器开发者工具 Cookies 中复制 `token`；
3. 在 `:8081` 粘贴 Access Token；
4. 添加 1–4 台打印机的 `名称 + Serial`；
5. 选择当前打印机并“保存并切换”。

Access Token 留空保存表示保留当前 Token。固件不会在状态 API/串口中回显 Token。可选“用 Token 获取我的打印机”只在用户主动点击时请求 Cloud，结果只填网页表单，Save 前不改 NVS。

设备端也可切换本地保存的打印机：

```text
GPIO0 short  -> 上一台
GPIO14 short -> 下一台
```

选择首尾循环并立即持久化；切换会清空旧打印机状态、断开旧 MQTT，再以新 Serial 重连，不需要重启、重新 Token 或验证码。

### Anti-flicker

Bambu 页面仍周期读取 service cache，但只有展示数据真实变化才 dirty。整屏清除只发生在首次进入或主动切换打印机；进度/温度/任务/耗材/footer 只局部重画，避免原先约 500 ms 一次的整屏闪烁。

### Cloud MQTT

```text
China:  cn.mqtt.bambulab.com:8883
Global: us.mqtt.bambulab.com:8883
username  = cloudUserId
password  = accessToken
subscribe = device/<activeSerial>/report
request   = device/<activeSerial>/request
```

只允许只读 `pushall` 状态同步 publish；receive buffer = 40960 bytes。rc 4/5 会进入 `token_invalid` 并停止用同一 Token 重试。

#### BambuHelper-aligned runtime

当前 MQTT 运行时高保真参考 `Keralots/BambuHelper` 的已验证 Cloud 连接模型，而不是继续使用前几轮 diagnostic timeout workaround：

```text
Arduino loop()
 -> BambuMqttService::process(nowMs)
 -> fresh WiFiClientSecure, strict CA, timeout 15 s
 -> fresh PubSubClient@2.8, buffer 40960, keepalive 30 s
 -> random bblp_* client ID
 -> mqtt.connect(cloudUserId, accessToken)
 -> subscribe report topic
 -> >=2 s 后发送一次 read-only pushall
```

关键差异：

- ESP32 平台升级为 `espressif32@6.12.0` / Arduino-ESP32 2.0.17，与参考实现对齐；
- 不再创建固定在 CPU0 的 `bambu-mqtt` 专用任务；
- PubSubClient 自己完成 TCP/TLS connect，不再在它前面显式建立第二套 TLS/preflight；
- 每次 Cloud 重连前彻底销毁旧 TLS/MQTT client；
- Cloud reconnect backoff = 30 s → 60 s（>=5 failures）→ 120 s（>=15 failures）；
- initial pushall 延迟 2 s，避免把 subscribe/connect/pushall 堆在同一个阻塞事务里；
- 允许像 BambuHelper 一样在已知长操作前 `esp_task_wdt_reset()`，但绝不关闭/删除 watchdog；
- 严格 CA，Bambu Cloud MQTT 禁止 `setInsecure()`。

前几轮 `mqtt_real_tls_*`、显式 `tls_->connect(...,5000)`、layered reconnect probe 和项目级 `MQTT_SOCKET_TIMEOUT=3/5` 已退出正常运行路径。

Bambu MQTT 是设备级后台服务，离开 Bambu App 不断开；Stock/Weather/HA 仍可发各自请求。连接/重连握手经 `NetworkArbiter` 串行化，已建立的持久 MQTT socket 不长期占用 arbiter。

## Home Assistant

只读 REST client：

```text
GET <base_url>/api/states/<entity_id>
Authorization: Bearer <Long-Lived Access Token>
```

1–4 entities，refresh 30–300 s。HTTP 仅可信 LAN；HTTPS 必须配置 CA，无 insecure fallback。

## Weather / Bad Apple

Open-Meteo；UI 显示 current + 今/明。Bad Apple：168×126 at x=152,y=27，2190 frames @ 10 FPS，silent loop，本地 flash。

## Stock / DeviceInfo

Stock：Tencent primary，EastMoney fallback；quote/intraday health 独立，失败保留 cache。

DeviceInfo：IP、SSID/RSSI/MAC、uptime/time、heap/min heap、PSRAM、Web 地址；不显示 secret。

## Architecture

```text
Stock -> dedicated MarketDataWorker
Weather + HomeAssistant -> one shared AppDataWorker
Bambu -> loop-driven persistent Cloud MQTT service + local multi-printer config
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
