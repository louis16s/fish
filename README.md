# ESP32-S3 双水位计闸门控制项目

![index1.jpeg](https://youke.xn--y7xa690gmna.cn/s1/2026/02/15/699139c9ab100.webp)

## 1. 项目功能概览

1. 通过 RS485 读取两个超声波液位计
- `ID001`：内塘
- `ID002`：外塘

2. 自动闸门控制
- 继电器1：开闸
- 继电器2：关闸
- 含滞回、最小动作间隔、超时保护、互锁保护

3. 中文网页控制面板
- 实时水位可视化
- 水位/温度/水位差/在线状态
- 自动模式与手动接管
- 闸门开关控制

4. OTA 升级
- `ElegantOTA` 页面升级：`http://<设备IP>/update`
![屏幕截图 2026-02-13 194838.png](https://youke.xn--y7xa690gmna.cn/s1/2026/02/13/698f0ff43e637.webp)

5. Air780E 4G 状态监测（AT）
- 周期检测在线、SIM、附着、信号
- 可通过配置总开关彻底关闭

## 2. 目录说明

1. `src/MAIN_ALL.ino`
- 主流程：传感器采集、自动闸门、告警、循环调度

2. `src/WS_MQTT.cpp`
- Wi-Fi连接、WebServer、面板静态文件服务（LittleFS）、MQTT控制、OTA入口

3. `data/ui/`
- 面板前端静态文件（`index.html/config.html/logs.html/pond_gate.svg`）
- 通过 PlatformIO `Upload Filesystem Image` 上传到设备 LittleFS（路径 `/ui/...`）后生效（无需刷固件即可更新 UI）

4. `src/WS_Serial.cpp`
- RS485串口初始化、Air780E AT状态轮询

5. `src/WS_GPIO.cpp`
- 继电器、RGB、蜂鸣器控制

6. `src/WS_Information.h`
- 核心配置中心（建议统一在此修改）

7. `doc/`
- 传感器资料与协议文档



## 3. 引脚定义

定义文件：`src/WS_GPIO.h`

1. RS485 传感器串口
- `TXD1 = GPIO17`
- `RXD1 = GPIO18`

2. 4G 模块 Air780E 串口
- `AIR780E_RXD = GPIO39`（ESP32接收）
- `AIR780E_TXD = GPIO40`（ESP32发送）

3. 继电器与外设
- `GPIO1`：继电器1（开闸）
- `GPIO2`：继电器2（关闸）
- `GPIO41`：继电器3
- `GPIO42`：继电器4
- `GPIO45`：继电器5
- `GPIO46`：继电器6
- `GPIO38`：RGB
- `GPIO21`：蜂鸣器

## 4. 配置说明（WS_Information.h）

仓库提供了示例文件：`src/WS_Information.example.h`。

### 4.1 功能开关

1. `MQTT_CLOUD_Enable`
- `true` 启用 MQTT 云连接

2. `WIFI_FallbackPortal_Enable`
- `true` 启用 WiFiManager AP 配网回退

3. `ELEGANT_OTA_Enable`
- `true` 启用 `/update` OTA 页面

4. `AIR780E_Enable`
- 4G 总开关
- `false` 时：
- 不初始化 Air780E 串口状态轮询
- 网页“设备信息”不显示 4G 项

### 4.2 Wi-Fi 相关

1. 预设网络
- `STASSID`
- `STAPSK`

2. 配网门户
- `WIFI_PORTAL_SSID`
- `WIFI_PORTAL_PASSWORD`
- `WIFI_PORTAL_TIMEOUT_S`

3. 本地持久化
- 固件会保存/读取 `LittleFS:/wifi.cfg`

### 4.3 自动闸门参数

1. 传感器映射
- `INNER_POND_SENSOR_ID`
- `OUTER_POND_SENSOR_ID`

2. 控制参数
- `GATE_OPEN_DELTA_THRESHOLD_MM`
- `GATE_CLOSE_DELTA_THRESHOLD_MM`
- `GATE_RELAY_ACTION_SECONDS`
- `GATE_MIN_ACTION_INTERVAL_S`
- `GATE_MAX_CONTINUOUS_RUN_S`
- `MANUAL_TAKEOVER_RECOVER_S`

### 4.4 采集与安全

1. `SENSOR_DATA_TIMEOUT_MS`
2. `LEVEL_JUMP_THRESHOLD_MM_PER_S`
3. `LEVEL_MIN_MM`
4. `LEVEL_MAX_MM`

### 4.5 日志与指示

1. `SERIAL_LEVEL_LOG_Enable`
2. `SERIAL_LEVEL_LOG_INTERVAL_MS`
3. `SERIAL_GATE_LOG_Enable`
4. `STARTUP_BUZZER_Enable`
5. `STARTUP_BUZZER_DURATION_MS`
6. `WIFI_OFFLINE_RGB_BLINK_Enable`
7. `WIFI_OFFLINE_RGB_BLINK_INTERVAL_MS`

## 5. Wi-Fi连接流程

`setup_wifi()` 的连接顺序：

1. 读取 `LittleFS:/wifi.cfg`
2. 尝试 NVS 已存 Wi-Fi（`WiFi.begin()` 无参数）
3. 尝试预设 `STASSID/STAPSK`
4. 若仍失败且启用回退，进入 WiFiManager 配网门户

连接成功后：

1. 写回当前 SSID/PSK 到 `wifi.cfg`
2. 启动 WebServer

## 6. 网页面板功能

访问：`http://<设备IP>/`

显示内容：

1. 内塘/外塘水位
2. 内塘/外塘温度
3. 水位差（内-外）
4. 闸门状态
5. 传感器在线状态（分两行）
6. 自动模式、手动接管状态
7. 互锁状态
8. 网络状态、设备信息、固件信息

控制按钮：

1. 开闸（继电器1）
2. 关闸（继电器2）
3. 停止动作
4. 开启自动闸门
5. 手动接管/恢复自动
6. 关闭自动（锁定关闭）
7. 单位切换（mm / m）
8. 打开 OTA 升级页

## 7. HTTP 接口

### 7.1 页面与数据

1. `GET /`
2. `GET /getData`
3. `GET /config`（控制策略配置页，存储在 LittleFS `/ctrl.json`）
4. `GET /logs`（日志查看页）
5. `GET /api/state`（统一状态接口，结构与 VPS 面板一致）
6. `POST /api/cmd`（统一命令接口，`{"cmd":"gate_open"}`）
7. `GET /api/config`（读取控制策略 JSON）
8. `POST /api/config`（写入控制策略 JSON）
9. `GET /api/log?name=error|measure|action&tail=16384`（读取日志末尾）
10. `POST /api/log/clear`（清空日志，JSON：`{"name":"error"}`）
11. `GET /update`
12. `GET /favicon.ico`

说明：

1. 面板前端页面文件存放在工程 `data/ui/`，固件从 LittleFS 提供这些资源（便于不刷固件更新 UI）。
2. LittleFS 覆盖路径为：
- `/ui/index.html`
- `/ui/config.html`
- `/ui/logs.html`
- `/ui/pond_gate.svg`（水位/水闸示意图）
3. 若需要覆盖/自定义 UI，请在 PlatformIO 执行 `Upload Filesystem Image`（或命令行 `pio run -t uploadfs`）。

### 7.2 闸门控制接口

1. `GET /GateOpen`
2. `GET /GateClose`
3. `GET /GateStop`
4. `GET /AutoGateOn`
5. `GET /AutoGateOff`
6. `GET /AutoGateLatchOff`
7. `GET /ManualEnd`

### 7.3 继电器兼容接口

1. `GET /Switch1` 到 `GET /Switch6`
2. `GET /AllOn`
3. `GET /AllOff`


## 8. /getData 字段说明

典型结构：

```json
{
  "sensor1": {"mm": 1234, "valid": true, "online": true, "temp_x10": 251, "temp_valid": true},
  "sensor2": {"mm": 1567, "valid": true, "online": true, "temp_x10": 248, "temp_valid": true},
  "gate_state": 0,
  "gate_position_open": false,
  "auto_gate": true,
  "auto_latched": false,
  "manual": {"active": false, "remain_s": 0, "total_s": 60},
  "relay1": 0,
  "relay2": 0,
  "net": {"wifi": true, "mqtt": true, "http": true, "ip": "192.168.1.5", "rssi": -57, "ssid": "MyWiFi"},
  "cell": {"enabled": true, "online": true, "sim_ready": true, "attached": true, "csq": 20, "rssi_dbm": -73, "last_rx_age_s": 2},
  "ctrl": {"open_allowed": true, "close_allowed": true, "cooldown_remain_s": 0, "min_interval_s": 15, "action_s": 10, "reason": ""},
  "alarm": {"active": false, "severity": 0, "text": "normal"},
  "fw": {"current": "v2026...", "latest": "ElegantOTA", "last_check": "25s", "last_result": "ready_update_page"}
}
```

状态说明：

1. `gate_state`
- `0`：待机
- `1`：开闸执行中
- `2`：关闸执行中

2. `cell.enabled`
- 由 `AIR780E_Enable` 决定

3. `alarm`（告警）
- `severity`：`0` 无告警，`1` 警告，`2` 严重
- `text`：告警文本（固件内部为英文，主页 UI 会映射为中文显示）
- 触发规则（见 `src/MAIN_ALL.ino`）：
- 严重（`severity=2`）
- 继电器互锁：开闸/关闸两个方向继电器同时为 ON（触发后立即 `Gate_Stop()`）
- 闸门动作超时：单次动作持续时间超过 `GATE_MAX_CONTINUOUS_RUN_S`
- 传感器离线/数据超时：任一传感器最后成功读数距离当前超过 `SENSOR_DATA_TIMEOUT_MS`
- 警告（`severity=1`）
- 水位突变：单位时间水位变化率超过 `LEVEL_JUMP_THRESHOLD_MM_PER_S`（触发后保持约 15s）
- 水位越界：水位小于 `LEVEL_MIN_MM` 或大于 `LEVEL_MAX_MM`（触发后保持约 15s）
- 清除：
- `severity=1` 的两类警告会在最后一次触发后约 15s 自动清除
- `severity=2` 中的“传感器离线/数据超时”会在数据恢复后自动清除
- `severity=2` 中的“互锁/动作超时”属于锁存类标志，通常在下一次成功开/关闸动作开始（或重启）后清除

## 9. MQTT 使用说明

### 9.1 当前实现范围

1. 已实现
- MQTT连接
- 下行控制解析
- 周期遥测上报（默认 `3s`）
- 状态变更触发上报（默认启用）

### 9.2 下行控制格式

主题：`MQTT_Sub`

JSON 示例：

```json
{"data":{"CH1":1}}
```

支持键：

1. `CH1` 到 `CH6`
2. `ALL`

取值：

1. `1`
- 执行动作

2. `0`
- 关闭或恢复

### 9.3 简化命令格式（新增）

主题：`MQTT_Sub`

JSON 示例：

```json
{"cmd":"gate_open"}
```

支持值：

1. `gate_open`
2. `gate_close`
3. `gate_stop`
4. `auto_on`
5. `auto_off`
6. `auto_latch_off`
7. `manual_end`

### 9.4 遥测上报格式

主题：`MQTT_Pub`

数据结构与 `GET /getData` 基本一致，示例：

```json
{"sensor1":{"mm":1234,"valid":true},"sensor2":{"mm":1567,"valid":true},"gate_state":0,"auto_gate":true}
```

## 9.5 控制策略（新增）

控制配置文件存储在 ESP32 LittleFS：`/ctrl.json`，可通过内网页面 `GET /config` 编辑。

支持三种机制（可配置多组）：

1. 定时（daily）：例如 08:00 开、09:00 关（开关可独立启用）
2. 循环（cycle）：例如 开 8h、关 3h、开 5h（支持 steps 多段循环）
3. 水位差（leveldiff）：内塘低于外塘开闸；内塘>=外塘关闸（阈值可配置）

模式 `mode` 支持：`mixed/daily/cycle/leveldiff`。

时间单位：

1. `tz_offset_ms`：时区偏移毫秒（例如 UTC+8 => `28800000`）
2. `daily.open_ms` / `daily.close_ms`：当天从 00:00 起的毫秒数（页面以 HH:MM 方式编辑）
3. `cycle.steps.dur_ms`：每段持续毫秒数

## 9.6 日志（新增）

日志写入 LittleFS（自动轮转）：

1. 错误日志：`/log_error.txt`
2. 测量日志：`/log_measure.txt`
3. 动作日志：`/log_action.txt`

日志格式：

1. 已同步时间：`YYYY-MM-DD HH:MM:SS [TAG] message`
2. 未同步时间：`ms=<millis> [TAG] message`

## 10. OTA 说明

当前固件采用 `ElegantOTA` 网页升级模式：

1. 访问 `http://<设备IP>/update`
2. 上传固件并执行升级


## 11. Air780E 说明

Air780E 状态轮询在 `src/WS_Serial.cpp`：

1. 轮询命令
- `AT`
- `AT+CPIN?`
- `AT+CSQ`
- `AT+CGATT?`

2. 状态字段
- 在线：`Air780E_Online`
- SIM就绪：`Air780E_SIMReady`
- 附着：`Air780E_Attached`
- 信号：`Air780E_CSQ` / `Air780E_RSSI_dBm`

3. 关闭方式
- `AIR780E_Enable false`

## 12. 构建配置

文件：`platformio.ini`

1. 板卡：`esp32-s3-devkitm-1`
2. Flash：`16MB`
3. 分区：`default_16MB.csv`
4. 监视器波特率：`115200`
5. 版本脚本：`scripts/auto_version.py`
6. 文件系统：`board_build.filesystem = littlefs`（用于上传 `data/` 前端静态文件）

## 13. 常见问题排查

1. 网页 404
- 确认串口有 `Web server started`
- 确认使用的是 `Web panel: http://<ip>/`

2. 传感器离线
- 检查 RS485 A/B 线和供电
- 检查地址冲突（001/002）
- 检查终端电阻和线缆长度

3. 4G信息不显示
- 检查 `AIR780E_Enable` 是否为 `true`
- 若为 `false`，网页设备信息会自动隐藏 4G字段

4. OTA 页面打不开
- 直接访问 `http://<设备IP>/update`


## 14. 当前代码边界（避免误解）

1. 面板前端文件位于 `data/ui/`：默认不内置进固件，需要上传到设备 LittleFS 才能访问（`/ui/...`）。
2. MQTT 已实现“遥测上报 + 下行控制”（见 `## 9. MQTT 使用说明`），但目前不包含：
- 云端下发/同步设备 `ctrl.json`（规则配置）
- 设备 LittleFS 日志上云（外网面板默认只提供“遥测历史回放”）

## 15. 云端外网面板（EMQX + 登录 + 历史回放）

仓库内提供了一个外网面板（独立 Web 应用，通过 MQTT 接入设备）：

- 代码目录：`server/`
- 主要能力：
- 服务端订阅设备遥测（MQTT）并写入 PostgreSQL
- 提供 HTTPS Web 面板（登录、用户管理）
- `历史水位`/`回放`：从服务器数据库查询历史数据（可导出 JSON/CSV）

OpenClaw 部署请直接看：`openclaw_read.md`（包含 `docker compose` 文件和安全建议）。
