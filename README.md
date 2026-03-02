# ESP32-S3 双水位计闸门控制系统

![index.jpeg](https://youke.xn--y7xa690gmna.cn/s1/2026/02/15/6991cc8c6c24c.webp)

本仓库是一个完整的端到端项目，包含三部分：

1. 固件（`firmware/`）
- 跑在 `ESP32-S3` 上，负责 RS485 采集、闸门控制、本地 Web 面板、MQTT 通信、日志。

2. 云端服务（`server/`）
- Node.js + PostgreSQL + MQTT。
- 提供登录鉴权、设备状态聚合、历史回放、规则读写、日志读写、管理员功能。
- 外网页面已移除独立“配置页”，管理能力统一走管理员 API（Web 主控页/小程序调用）。

3. 微信小程序（`little_program/`）
- 面向移动端，使用 `server/` 提供的 API 进行控制与管理。

## 1. 仓库结构

- `firmware/`：ESP32 固件工程（PlatformIO）
- `firmware/src/`：ESP32 固件源码
- `firmware/data/ui/`：设备端 Web 面板静态资源（LittleFS + 固件内嵌双兜底）
- `firmware/scripts/`：PlatformIO 构建脚本（自动版本、UI 资源嵌入、合并 BIN）
- `server/`：云端控制面板服务
- `little_program/`：微信小程序
- `openclaw/`：Docker Compose 部署模板（EMQX/Postgres/Server/Caddy）
- `doc/`：硬件资料、截图、部署说明补充

## 2. 系统架构与数据流

### 2.1 设备侧

1. 采集
- 两个 RS485 超声波液位计。
- `sensor1` 对应内塘，`sensor2` 对应外塘（以 `WS_Information.h` 实际配置为准）。

2. 控制
- 闸门开/关由继电器控制。
- 带最小动作间隔、超时保护、互锁保护、手动接管保护。

3. 对外接口
- 本地 HTTP 页面与 API。
- MQTT 遥测上报、命令下发、RPC（配置/日志）。

### 2.2 云端侧（server）

1. MQTT 订阅设备遥测并落库 PostgreSQL。
2. 提供 HTTPS API 给 Web 面板和微信小程序。
3. 将控制命令与配置/日志 RPC 通过 MQTT 转发给设备。
4. 提供日志推送缓存，避免代理超时导致日志页不可用。

### 2.3 移动端（小程序）

1. 登录后读取设备列表。
2. 周期拉取 `/api/state` 获取实时状态。
3. 下发 `/api/cmd` 控制闸门。
4. 使用 `/api/config` 编辑控制策略。
5. 使用 `/api/history`、`/api/telemetry/range` 做历史回放与导出。

## 3. 快速开始

### 3.1 固件（PlatformIO）

1. 进入固件目录
```bash
cd firmware
```

2. 准备配置文件
- 在 `firmware/` 目录内复制 `src/WS_Information.example.h` 为 `src/WS_Information.h`（仓库根目录等价路径为 `firmware/src/...`），填入 Wi-Fi、MQTT、阈值等参数。

3. 构建与烧录
```bash
pio run
pio run -t upload
```

4. 上传文件系统（推荐）
```bash
pio run -t uploadfs
```

说明：
- 当前 `firmware/platformio.ini` 已启用 `scripts/embed_ui_assets.py`，构建时会把 `data/ui/*` 生成到 `src/WS_UI_Assets.*`。
- 同时也支持 `uploadfs` 上传 LittleFS 资源。运行时优先读取 LittleFS；LittleFS 缺失时回退到固件内嵌资源。

### 3.2 云端服务（server）

1. 进入目录并安装依赖
```bash
cd server
npm install
```

2. 配置环境变量
```bash
copy .env.example .env
```
- 至少填写：`SESSION_SECRET`、`ADMIN_PASSWORD`、`DATABASE_URL`、`MQTT_URL`、`MQTT_USERNAME`、`MQTT_PASSWORD`。

3. 启动
```bash
npm run start
```

4. 健康检查
- `GET /healthz` 应返回 `ok=true`。

详细部署与 API 请看：`server/README.md`。

### 3.3 微信小程序（little_program）

1. 修改 `little_program/utils/config.js` 中 `BASE_URL`。
2. 微信后台配置 request 合法域名（HTTPS）。
3. 用微信开发者工具导入 `little_program/` 运行。

详细页面与接口说明请看：`little_program/README.md`。

## 4. 固件能力清单

### 4.1 采集与状态

- 水位 `mm`、温度 `temp_x10`、有效性 `valid/temp_valid`、在线状态 `online`。
- 网络状态 `net`（wifi/mqtt/http/ip/rssi/ssid）。
- 可选 4G 模块状态 `cell`（由 `AIR780E_Enable` 开关控制）。
- 继电器状态 `relay1..relay6`（其中 `relay3..relay6` 对应三色灯+蜂鸣器）。

### 4.2 闸门控制

支持命令：

- `gate_open`
- `gate_close`
- `gate_stop`
- `auto_on`
- `auto_off`
- `auto_latch_off`
- `manual_end`
- `signal_red_toggle` / `signal_red_on` / `signal_red_off`
- `signal_yellow_toggle` / `signal_yellow_on` / `signal_yellow_off`
- `signal_green_toggle` / `signal_green_on` / `signal_green_off`
- `signal_buzzer_toggle` / `signal_buzzer_on` / `signal_buzzer_off`
- `signal_all_off`

通道映射：

- `CH1`：开闸继电器
- `CH2`：关闸继电器
- `CH3`：红灯
- `CH4`：黄灯
- `CH5`：绿灯
- `CH6`：蜂鸣器

CH3-CH6 联动规则（默认）：

- `CH3` 红灯：无网络常亮；有网络且任一传感器离线持续 10s 后闪烁；其余熄灭。
- `CH4` 黄灯：闸门关闭态亮。
- `CH5` 绿灯：闸门打开态亮。
- `CH4+CH5`：手动接管期间同时亮（手动接管优先于闸门态）。
- `CH6` 蜂鸣器：上电提示音 A；Wi-Fi 从离线恢复在线时提示音 B（与主控联网成功提示音一致）。

外接三色灯颜色建议：

- 红（CH3）：离线/故障提示。
- 黄（CH4）：关闸态或手动接管态。
- 绿（CH5）：开闸态或手动接管态。
- 手动接管 ：黄绿同亮。
![三色灯1.jpg](https://picui.ogmua.cn/s1/2026/03/02/69a5158e50321.webp)
兼容性说明：

- 仍保留 `signal_*` 命令和 `Switch3..6` 兼容路由。
- 但 CH3-CH6 由自动状态机主导，手动命令设置可能在下一轮状态刷新时被覆盖。
- 互锁与安全只约束 CH1/CH2；`ALL_ON` 不会同时吸合 CH1/CH2。

控制策略（`/ctrl.json`）：

- `daily`：定时开/关（支持多组、周掩码、开关独立启用）
- `cycle`：循环步骤（开/关 + 持续时长）
- `leveldiff`：水位差阈值控制
- `mode`：`mixed | daily | cycle | leveldiff`

### 4.3 日志

设备侧 LittleFS 日志文件：

- `/log_error.txt`
- `/log_measure.txt`
- `/log_action.txt`

支持 MQTT 日志推送主题：

- `<device_id>/device/log/error`
- `<device_id>/device/log/measure`
- `<device_id>/device/log/action`

## 5. 设备本地 HTTP 接口

### 5.1 页面

- `GET /`
- `GET /config`
- `GET /logs`
- `GET /update`

### 5.2 状态与控制

- `GET /getData`
- `GET /api/state`
- `POST /api/cmd`
- `GET /api/config`
- `POST /api/config`
- `GET /api/log`
- `POST /api/log/clear`

兼容接口：

- `GET /GateOpen`、`/GateClose`、`/GateStop`
- `GET /AutoGateOn`、`/AutoGateOff`、`/AutoGateLatchOff`、`/ManualEnd`
- `GET /Switch1..6`、`/AllOn`、`/AllOff`

说明：

- 推荐统一走 `POST /api/cmd`，兼容路由主要用于旧版页面和调试。

## 6. MQTT 主题建议

设备默认语义：

- 上行遥测：`<device_id>/device/telemetry`
- 上行 RPC 回复：`<device_id>/device/reply`
- 上行日志推送：`<device_id>/device/log/<name>`
- 下行命令/RPC：`<device_id>/device/command`

ACL 最小权限建议：

1. 设备账号
- 发布：`<device_id>/device/telemetry`、`<device_id>/device/reply`、`<device_id>/device/log/#`
- 订阅：`<device_id>/device/command`

2. 服务器账号
- 订阅：`+/device/telemetry`、`+/device/reply`、`+/device/log/#`
- 发布：`+/device/command`

## 7. 关键配置项（固件）

请以 `firmware/src/WS_Information.h` 为准，重点关注：

1. 网络与服务
- `STASSID` / `STAPSK`
- MQTT Broker 与账号

2. 策略与安全
- `GATE_OPEN_DELTA_THRESHOLD_MM`
- `GATE_CLOSE_DELTA_THRESHOLD_MM`
- `GATE_MIN_ACTION_INTERVAL_S`
- `GATE_MAX_CONTINUOUS_RUN_S`
- `SENSOR_DATA_TIMEOUT_MS`

3. 功能开关
- `MQTT_CLOUD_Enable`
- `WIFI_FallbackPortal_Enable`
- `ELEGANT_OTA_Enable`
- `AIR780E_Enable`

## 8. 典型联调顺序

1. 先跑通设备本地面板
- 保证 `http://<设备IP>/` 可访问，`/getData` 有数据。

2. 再接入 MQTT 与云端
- `server/healthz` 中 `mqtt=true`、`db=true`。

3. 最后接入小程序
- `BASE_URL` 指向云端地址，登录后能看到设备列表与状态。

## 9. 常见问题

1. 面板空白或 404
- 先执行 `pio run -t uploadfs`。
- 若仍异常，检查串口日志是否有 LittleFS 挂载失败；固件内嵌资源会兜底但不保证你本地修改已生效。

2. 小程序控制返回 502
- 云端 MQTT 未连接，检查 `server/.env` MQTT 配置与 Broker ACL。

3. 小程序控制返回 504
- 设备在线性差或离线，MQTT RPC 超时。

4. 设备列表为空
- 设备必须先上报 telemetry，`server` 才会在 `devices` 表记录。

## 10. 相关文档

- 服务端：`server/README.md`
- 小程序：`little_program/README.md`
- 一键部署参考：`openclaw/openclaw_read.md`
- RS485 传感器说明：`doc/rs_485_ultrasonic_level_meter_readme.md`
