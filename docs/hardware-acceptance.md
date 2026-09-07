# T-Display-S3 Hardware Acceptance

只用于 **LILYGO T-Display-S3 真机**。Host tests / CI build 不能替代实体板验收。

每次记录：branch、完整 source SHA、Actions run、artifact、firmware SHA256、日期、Wi-Fi、串口和结果。

## Artifact / Flash

开发侧必须已通过：

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

Codex 下载 exact-SHA `tdisplay-gp-firmware-<SOURCE_SHA>` 并确认：

```text
manifest source_sha == approved SHA
actual firmware.bin SHA256 == manifest firmware_sha256
```

正常升级仅写 `firmware.bin` 到 manifest `firmware_offset`（当前通常 `0x10000`）。不得 erase NVS，不重写 bootloader/partition table。Serial 115200。

## Display / Input / Startup

- 320×170 landscape rotation 3，无裁切/花屏/残留。
- 中文可读。
- 菜单恰好五项且顺序正确：**股票、天气、Bambu Lab、智能家居、设备信息**。
- 40 ms debounce、700 ms long、long release 不产生额外 short。
- 重启直接进入 **Stock**。
- Menu / Stock / Weather / Bambu / Home Assistant / DeviceInfo 任一页面无按键停留 >60 s 仍保持该页面；网络刷新不得自动切 App。

## Weather / Bad Apple

左侧：current condition/temp/apparent/humidity/wind/rain/update 可读；底部只显示紧凑 **今 / 明**，不得显示“后天”。

右侧：

- 168×126，约 x=152,y=27 至屏幕右侧/接近底部；
- 无贯穿顶部横向分隔线；无天气/视频之间竖向分隔线；
- Bad Apple 黑白轮廓可辨认，约 10 FPS，不能 100 ms 整屏闪黑；
- 前台约 220 s 后完整序列回到开头继续循环；
- 离开 Weather 后停止视频后台重绘；重新进入从开头播放；
- 播放不增加 network/AppDataWorker 请求；
- Weather 5 min 无 watchdog/panic/reboot/freeze，heap 不持续单向下降。

天气成功后断网：旧 cache 保留，无 tight retry；Bad Apple 本地视频继续播放。

## Unified Integrations portal

打开：

```text
http://<device-ip>:8081/
```

必须只有一个共享 Integrations 页面，并同时出现 Home Assistant 与 Bambu Lab Cloud 配置区。

本地 8081 为 HTTP，只在可信 LAN 使用。

Bambu 账号输入框必须是通用账号输入，不得使用浏览器 `type=email` 强制阻止中国区手机号。页面应明确提示 **中国区手机号 / Global 邮箱**。

若 Cloud 返回验证 challenge，验证码区域仅在 `verification_required` 时出现。SMS/email 可显示手动重发按钮与本地冷却；TFA 不应提供重发动作。

## Home Assistant regression

T-Display 继续作为用户现有 HA server 的只读 REST client。

- HTTP 模式无需 CA、能读取配置实体；仅可信 LAN。
- HTTPS 模式若测试：正确 CA 成功，错误/缺失 CA 不允许 insecure fallback。
- 1–4 entity state/unit/label 可见，失败后保留 last-valid state。
- `/api/ha/status` 不含 Token/CA 内容，只允许存在性标志。
- Serial 不出现 HA Token/Authorization header。

## Bambu Lab Cloud — secret safety

**账号密码和一次性验证码只在本地 `:8081` 页面直接输入，不发给 ChatGPT/Codex，不放入串口报告/截图。**

检查：

- Bambu status 不回显 password/access token，只显示 `password_set` / `token_set` 等存在性；
- verification status 只允许返回 `verification_required`、typed channel 和 resend delay 等非秘密元数据；
- `tfaKey`/challenge secret 不得出现在 NVS、status API、串口或截图；
- 一次性 verification code 不持久化、不回显、不打印；
- 密码框留空保存不会意外把已保存密码变成明文返回；
- logout 能清除密码、Token、cloud user ID 和打印机选择；
- Serial 不出现密码、Token、验证码、完整 Authorization/Cloud auth response body。

## Bambu Lab Cloud — login / verification / printer discovery

1. 选正确 region；
2. **China：本地输入中国大陆手机号 + 密码（该账号若使用邮箱也可输入邮箱）；Global：输入邮箱 + 密码**；
3. 点击登录后，本地表单/固件不得因为“不是邮箱”而拒绝合法中国手机号；
4. 若 Cloud 无 challenge：直接继续登录成功；
5. 若 Cloud 返回 challenge：状态进入 `VERIFICATION_REQUIRED`，并正确区分 SMS / email / TFA；
6. 在本地 `:8081` 输入一次验证码并提交。成功后应保存 replacement token，并自动继续 user ID / printer discovery / MQTT 恢复；
7. SMS/email 若测试重发，只手动触发一次，并确认 60 秒本地 cooldown 防止连续重发；TFA 不应允许 resend；
8. printer picker 能列出账号绑定设备；
9. 保存选中打印机并重启/应用；
10. Bambu 页面从 UNCONFIGURED/VERIFY/CONNECTING 进入在线/有效状态；
11. 成功验证后正常重启、保留 NVS；若 stored token 仍有效，不应再次要求验证码。

China 手机号验收至少覆盖真实的 11 位大陆移动号码格式；固件也接受 `+86`/`86` 前缀。账号字符串最终应作为 Cloud 登录 JSON 的 `account` 字段提交。

Cloud challenge 是正常的人工恢复流程，不是“2FA blocked”失败。不得绕过第二因素，也不得自动/高频重发验证码。

## Bambu config concurrency / stale-write protection

运行时 Bambu 配置必须由 `BambuMqttService` 单点持有，Portal/UI 只通过线程安全 snapshot/update 访问。自动测试必须证明：Portal external update 发生后，基于旧 revision 的后台 token/user ID/printer discovery 写回会被拒绝，不能覆盖新的 NVS/runtime config，也不能重新发布旧 printer list。

verification 成功产生的新 token 也属于 revision-guarded writeback：若 challenge 期间用户已经提交更新配置，旧 challenge 结果不得覆盖较新的 Portal 配置。

真机若能安全制造时序：在后台正在连接、重新登录、验证或发现打印机期间，于 `:8081` 提交较新的有效 Bambu 配置；后续晚到的旧 Cloud 结果不得把页面/重启后的配置恢复到旧值。因为保存会很快重启且不应为了制造竞态反复错误登录，无法稳定复现时允许标记 `NOT TESTED`，不要进行可能触发账号锁定的实验。

## Bambu Cloud MQTT / remote reachability

Cloud brokers：China `cn.mqtt.bambulab.com:8883`；US/EU `us.mqtt.bambulab.com:8883`。

验证：

- 选中设备后订阅 `device/<serial>/report`，打印机状态能持续更新；
- V1 仅允许 read-only `pushall` 状态同步，不测试/执行任何打印机控制命令；
- **关键远程场景：** 在不破坏其他网络的前提下，让 T-Display 无法直达打印机 LAN、但仍可访问 Internet，确认仍能通过 Bambu Cloud 获取打印机状态；
- 不要修改公司/家庭网络基础设施来强行制造场景；可使用安全的独立热点/不同网络完成远程验证。

## Bambu screen live state

在打印中观察：

- friendly printer name + connection/print state；
- progress 百分比/进度条；
- ETA/remaining minutes；
- layer current/total；
- nozzle current/target；
- bed current/target；
- chamber temp（机型/报告提供时）；
- job/subtask name；
- active filament/AMS slot/type/color（报告提供时）。

缺失字段可以显示占位，不得用垃圾值导致崩溃。Partial report 不应清空旧的有效字段。

## Bambu background freshness

1. 在 Bambu 页面确认初始状态。
2. 切到 Stock/Weather/HA/DeviceInfo，停留足够时间让打印进度发生变化。
3. 返回 Bambu。
4. 应直接看到更新后的 cache；不应因为离开 Bambu 而断开 MQTT 或重新从零开始等待完整状态。
5. MQTT connected 时 Stock/Weather/HA 仍可正常进行各自短生命周期网络请求，不得被永久阻塞。

## Wi-Fi loss / recovery

在可控条件下中断 T-Display Wi-Fi 后恢复：

- last valid printer state 保留；
- connectivity 显示 offline/connecting；
- Wi-Fi 恢复后 Cloud MQTT 自动重连；
- 无 watchdog、panic、unexpected reboot、freeze；
- 无明显单向 heap leak。

## Token renewal / verification recovery

只在**安全且不会造成账号锁定**的方式下测试。

期望：Token 无效/MQTT auth rc 4/5，且已保存密码时，进入自动 relogin，使用已保存的账号标识（China 可为手机号）取得新 Token/User ID 并重新连接。

若自动 relogin 无 challenge，整个流程无人干预完成。若 Cloud 在 renewal 阶段要求 SMS/email/TFA，则 unattended flow 停在 `VERIFICATION_REQUIRED`，不会继续撞登录/重发接口；用户在 `:8081` 提交一次验证码后，服务保存 replacement token 并自动恢复 identity/discovery/MQTT。

失败退避约：

```text
1 min -> 5 min -> 15 min -> 30 min max
```

不要为了测试而连续提交错误密码、错误验证码或反复撞 Bambu Cloud。若无法安全制造 Token 失效，标记 `NOT TESTED`，不影响普通 Cloud MQTT smoke，但影响“完整自动续期/验证恢复验收”的结论。

## Stock regression

- Stock >60 s 保持 Stock；
- 切股/颜色/quote/intraday/chart 正常；
- Tencent 默认 primary，EastMoney fallback 逻辑未回归；
- quote/intraday health 独立；
- cache 保留。

## Shared worker / concurrency

快速切 Weather/HA/Bambu/Menu/Stock/DeviceInfo：

- Stock 使用专用 MarketDataWorker；
- Weather/HA 共用一个 AppDataWorker，late results 不串 App；
- inactive Weather/HA 不因 late completion 重绘当前 TFT；
- Bambu Cloud MQTT 是独立后台服务，App transition 不控制其连接；
- Bambu MQTT connect/reconnect handshake 才参与 NetworkArbiter；建立后持久 socket 不长期持有 arbiter；
- Bambu mutable config 和 verification challenge 只有一个 service owner；Portal update 优先于旧后台结果；
- Bad Apple 不新增网络/worker/arbiter traffic；
- DeviceInfo local-only。

## Stability

收集 `[sys]` 应只出现：

```text
MENU|STOCK|WEATHER|BAMBU|HOME_ASSISTANT|DEVICE_INFO
```

正式 full acceptance 至少覆盖：Stock 10 min、Weather >=5 min、HA success、Bambu login/verification（适用时）、active print/background freshness、Wi-Fi interruption/recovery、100 transitions。

100 次跨 App/menu transition：watchdog=0、unexpected reboot=0、freeze=0、short-after-long=0、明显 heap leak=0、background wrong-screen redraw=0。

## Final report

```text
SOURCE SHA:
ACTIONS RUN / ARTIFACT:
PORT:
FIRMWARE SHA256:
FLASH: PASS/FAIL
DISPLAY/INPUT: PASS/FAIL
STOCK DEFAULT START: PASS/FAIL
FIVE-APP MENU: PASS/FAIL
NO AUTO-IDLE SWITCH: PASS/FAIL
WEATHER LEFT LAYOUT: PASS/FAIL
WEATHER NO DIVIDERS: PASS/FAIL
BAD APPLE 168x126 / ~10FPS: PASS/FAIL
BAD APPLE ~219S LOOP: PASS/FAIL/NOT TESTED
BAD APPLE EXIT/REENTER: PASS/FAIL
WEATHER NETWORK/CACHE: PASS/FAIL
UNIFIED :8081 PORTAL: PASS/FAIL
HOME ASSISTANT HTTP: PASS/FAIL/NOT TESTED
HOME ASSISTANT HTTPS CA: PASS/FAIL/NOT TESTED
HA SECRET LEAK: PASS/FAIL
BAMBU CHINA PHONE LOGIN: PASS/FAIL/NOT APPLICABLE
BAMBU LOGIN: PASS/FAIL/VERIFICATION_REQUIRED/NOT TESTED
BAMBU VERIFICATION FLOW: PASS/FAIL/NOT APPLICABLE/NOT TESTED
BAMBU VERIFICATION SECRET LEAK: PASS/FAIL
BAMBU VERIFICATION RESEND COOLDOWN: PASS/FAIL/NOT APPLICABLE/NOT TESTED
BAMBU TOKEN REBOOT REUSE: PASS/FAIL/NOT TESTED
BAMBU PRINTER DISCOVERY: PASS/FAIL/NOT TESTED
BAMBU CONFIG STALE-WRITE: PASS/FAIL/NOT TESTED
BAMBU CLOUD MQTT: PASS/FAIL/NOT TESTED
BAMBU REMOTE-NETWORK TEST: PASS/FAIL/NOT TESTED
BAMBU LIVE FIELDS: PASS/FAIL/PARTIAL/NOT TESTED
BAMBU BACKGROUND FRESHNESS: PASS/FAIL/NOT TESTED
BAMBU SECRET LEAK: PASS/FAIL
BAMBU WIFI RECOVERY: PASS/FAIL/NOT TESTED
BAMBU TOKEN AUTO-RELOGIN: PASS/FAIL/NOT TESTED
STOCK/TENCENT: PASS/FAIL
TX->EM FALLBACK: PASS/FAIL/NOT TRIGGERED
100-TRANSITION SOAK: PASS/FAIL/NOT TESTED
HEAP/STABILITY: PASS/FAIL
FULL HARDWARE ACCEPTANCE: PASS/FAIL/PARTIAL
```

FAIL 附原始串口、复现步骤、时间点和照片/截图（适用时），但先主动删去/打码任何 password、token、verification code、challenge secret。