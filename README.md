# T-Display GP

LILYGO T-Display-S3 320×170 多应用桌面终端。

当前正式应用壳：

```text
股票 → 天气 → Bambu Lab → 智能家居 → 设备信息
```

GitHub exact SHA 是项目事实源。

## Startup / Navigation

- **默认启动 Stock。**
- **没有自动 idle 跳转。** Menu / Stock / Weather / Bambu Lab / Home Assistant / DeviceInfo 都会保持当前页面，直到用户主动按键切换。
- `AppManager` 不维护无操作计时器，也不会因为网络/数据活动自动切 App。

按键：

```text
normal app: GPIO0 short prev; GPIO14 short next; GPIO0 long menu; GPIO14 long no-op
menu:       GPIO0 short prev; GPIO14 short next; GPIO0 long no-op; GPIO14 long enter
```

40 ms debounce，700 ms long，long release 不产生 short。

## Bambu Lab Cloud

T-Display 是 **只读 Cloud 监视器**，不是打印机控制器。V1 不要求与打印机同一 LAN，也不开放/依赖打印机本地 MQTT 8883；不实现 pause/resume/stop、温度、灯光、相机等控制。

配置入口与 Home Assistant 共用一个本地 Integrations 页面：

```text
http://<T-Display-IP>:8081/
```

在该页面本地填写：

- region：China 或 US/EU/Global；
- Bambu 账号：**China 可使用中国大陆手机号（11 位，亦接受 +86/86 前缀）或邮箱；US/EU/Global 使用邮箱**；
- 账号密码（可选择保存，默认用于 Token 自动续期）；
- 登录后从账号绑定设备中选择打印机。

内部配置字段仍沿用历史名称 `email` 以保持 NVS/schema 兼容，但在 China region 下它表示通用 Bambu `account`，可以保存手机号；Cloud 密码登录实际向 Bambu `/v1/user-service/user/login` 提交 `{account,password}`。

**不要把 Bambu 密码或 Access Token 发到聊天、日志或截图中。** 固件的 status API 只返回 `password_set` / `token_set` 之类的存在性布尔值，不回显秘密；串口也不打印密码、Token 或完整 Cloud 响应体。

Cloud 流程：

```text
verified HTTPS login / identity / printer discovery
  -> access token + cloud user id
  -> verified TLS MQTT broker
  -> subscribe device/<serial>/report
  -> background BambuState cache
  -> passive BambuApp renderer
```

Broker：

```text
China: cn.mqtt.bambulab.com:8883
US/EU/Global: us.mqtt.bambulab.com:8883
```

MQTT username = cloud user id，password = access token。连接后只允许发送一次只读状态同步 `pushall` 请求到 `device/<serial>/request`，之后消费 `device/<serial>/report`；V1 不发布打印机控制命令。

Bambu HTTPS 与 MQTT 都使用 CA 校验；Bambu 凭据路径禁止 `setInsecure()`。MQTT receive buffer 为 40960 bytes；分配失败进入可见错误状态，不重启设备。

MQTT 是设备级后台服务，不跟随 Bambu 页面启停。离开 Bambu 后连接继续维护，返回页面应看到最新 cache。只有 HTTPS 请求和 MQTT connect/reconnect handshake 使用 `NetworkArbiter`；已建立的持久 MQTT socket 不长期占用 arbiter，从而不阻塞 Stock/Weather/HA。

`BambuMqttService` 是运行时 Bambu 配置的唯一可变所有者。Portal 与 UI 只能通过 mutex 保护的 snapshot/update 接口访问配置；后台登录、User ID 与打印机发现产生的持久化写回必须携带取得快照时的 external revision。若期间用户已经在 `:8081` 提交更新，旧后台结果会被判定为 stale 并丢弃，不允许覆盖较新的账号、Token 或打印机选择。

Access Token 失效且已保存密码时，固件自动重新登录并持久化新 Token/User ID，然后重新连接 MQTT。失败退避为约 **1 min → 5 min → 15 min → 30 min（封顶）**。若账号要求 2FA/email-code，V1 明确显示需要二次认证并停止无人值守续期，不尝试绕过。

Bambu 页面优先显示打印机名/连接状态、打印进度、ETA、层数、喷嘴/热床/腔体温度、任务名、当前耗材/AMS 信息（字段可用时）。

## Home Assistant

**用户已有 Home Assistant 是服务器；T-Display-S3 只是只读 REST API 客户端。**

```text
T-Display -> GET <existing HA>/api/states/<entity_id>
```

- V1 read-only，不调用 `/api/services`。
- 1–4 entities，optional labels。
- refresh 30–300 s，default 30 s，active-only。
- per-entity last-valid cache。
- Bearer Long-Lived Access Token。

HA 与 Bambu 共用 `http://<T-Display-IP>:8081/` 配置页。HA HTTP 模式只适合可信 LAN；HTTPS 必须 PEM CA + `setCACert()`，禁止 credentialed HA HTTPS 使用 `setInsecure()`。

## Stock

Tencent quote+intraday primary，EastMoney fallback；quote/intraday health 独立；quote 优先；intraday latest-wins；有界 retry；cache-preserving。Stock 继续使用专用 `MarketDataWorker`。

## Weather / Bad Apple

Open-Meteo 获取 current + 3-day structured data；默认 15 min，5–60 min；active-only；failure 保留 cache。UI 只显示 current + 今/明，`dayAfter` 仍保留在 provider/cache 中但不渲染。

Bad Apple：

```text
viewport x=152, y=27, 168×126
2190 frames @ 10 FPS ≈ 219 s
1-bit monochrome, silent, loop
```

- 不显示“后天”。
- 不绘制贯穿顶部的横线，也不绘制左侧天气与视频之间的竖线。
- 进入 Weather 从 frame 0 开始；离开立即停止视频刷新。
- 只刷新视频 viewport，不做 10 FPS 整屏清屏。
- 本地 flash asset，无 runtime HTTP/task/AppDataWorker/NetworkArbiter 流量。
- 构建时从锁定源生成，校验 Git blob SHA1 与全部 2190 帧 delta round-trip；原始 MP4/生成 C++ asset 不提交仓库。
- 项目许可证不重新授权 Bad Apple!! 原始 PV/音乐。

## DeviceInfo

IP、SSID/RSSI/MAC、uptime/time、heap/min heap、PSRAM、Web 地址；local-only，不显示 secrets。

## Architecture

```text
Stock -> dedicated MarketDataWorker
Weather / HomeAssistant -> exactly one shared AppDataWorker -> typed result queues
Bambu -> dedicated persistent Cloud MQTT service + BambuState cache
DeviceInfo -> local-only
Bad Apple -> local flash playback only
```

短生命周期 external HTTP/TLS 请求统一通过 `NetworkArbiter`。Bambu MQTT connect/reconnect handshake 也通过 arbiter；成功连接后专用持久 socket 释放 arbiter并独立维持 keepalive/report traffic。

FreeRTOS queues 传 request/result pointer，不 raw-copy 含 `std::string` 的对象。

## Config / Security

- AppConfig schema v2 / `stockticker` NVS。
- HA 使用独立 `ha_config` blob。
- Bambu 使用独立 `bambucloud` NVS namespace/blob。
- Bambu runtime config 由 `BambuMqttService` 单点持有；Portal external update 递增 revision，后台衍生写回必须 revision-match 才能提交。
- 普通 firmware upgrade 保留 NVS。
- 端口 8081 只有一个 `IntegrationConfigPortal`，同时提供既有 HA routes 和 Bambu routes。
- No secret logging。

## Diagnostics

```text
[md]      Stock
[appdata] WEATHER / HOME_ASSISTANT
[net]     actual short-lived transport; HA mode=HA_HTTP or HA_CA
[sys]     MENU/STOCK/WEATHER/BAMBU/HOME_ASSISTANT/DEVICE_INFO
```

Bambu 状态通过 UI/secret-safe status 暴露；不要依赖或添加凭据日志。

## Verification

Host/CI 的 Bad Apple asset generation 需要 ffmpeg。

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

CI 同时跑 Windows native，并发布 exact-SHA artifact：`firmware.bin`、`partitions.bin`、`bootloader.bin`、`firmware-manifest.txt`。

Codex 只烧录/验证预编译 exact-SHA application image；普通测试不本地重编译、不 erase NVS、不改 partition/bootloader。

详细说明：`docs/deployment.md`、`docs/api-contract.md`、`docs/hardware-acceptance.md`。