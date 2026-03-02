# Firmware README

## 1. 项目说明

本目录是 `ESP32-S3` 固件工程（PlatformIO），负责：

- RS485 水位/温度采集
- CH1-CH6 继电器控制
- 本地内网页面与 API（`/`、`/api/state`、`/api/cmd` 等）
- MQTT 遥测、命令与 RPC
- LittleFS 日志与配置持久化

## 2. 继电器通道定义

当前 6 路继电器分配如下：

1. `CH1`：闸门开闸（OPEN）
2. `CH2`：闸门关闸（CLOSE）
3. `CH3`：三色灯红灯
4. `CH4`：三色灯黄灯
5. `CH5`：三色灯绿灯
6. `CH6`：蜂鸣器

说明：

- `CH3-CH6` 由“信号塔自动状态机”主导（非纯手动）。
- 闸门安全互锁仅作用于 `CH1/CH2`，不会阻塞 `CH3-CH6` 的状态切换。

## 3. 信号塔联动规则（CH3-CH6）

优先级（高 -> 低）：

1. 手动接管优先（`Manual_Takeover_Active`）
2. 网络状态优先于传感器离线（用于 CH3）
3. 闸门状态（用于 CH4/CH5）

默认参数：

- `SIGNAL_SENSOR_LOST_HOLD_MS = 10000`（任一传感器离线持续 10s 才进入告警闪烁）
- `SIGNAL_RED_BLINK_MS = 500`（红灯翻转周期 500ms）
- `SIGNAL_WIFI_BEEP_MIN_GAP_MS = 5000`（联网提示音最小间隔）

行为真值表：

| 条件 | CH3 红灯 | CH4 黄灯 | CH5 绿灯 | CH6 蜂鸣器 |
|---|---|---|---|---|
| 无网络 | 常亮 | 按手动/闸门规则 | 按手动/闸门规则 | 无网络本身不触发 |
| 有网络 + 任一传感器离线持续>=10s | 闪烁 | 按手动/闸门规则 | 按手动/闸门规则 | 不触发 |
| 有网络 + 传感器正常 | 熄灭 | 按手动/闸门规则 | 按手动/闸门规则 | 不触发 |
| 手动接管 | 不变（仍由网络/传感器决定） | 常亮 | 常亮 | 不触发 |
| 闸门打开态 | 不变 | 熄灭 | 常亮 | 不触发 |
| 闸门关闭态 | 不变 | 常亮 | 熄灭 | 不触发 |
| 上电 | 不变 | 不变 | 不变 | 播放提示音 A |
| Wi-Fi 离线->在线 | 不变 | 不变 | 不变 | 播放提示音 B（与主控联网成功提示音完全同节奏） |

## 4. 内网页面（本地面板）更新点

`data/ui/index.html` 已增加“**三色灯与蜂鸣器（CH3-CH6）**”控制区：

- 4 个状态卡片（红灯/黄灯/绿灯/蜂鸣器），实时显示 ON/OFF。
- 4 个“切换”按钮：分别切换 CH3/CH4/CH5/CH6。
- 1 个“全部关闭”按钮：关闭 CH3-CH6（不影响 CH1/CH2）。
- 和闸门控制共用同一命令通道（优先 `POST /api/cmd`，兼容老式回退路由）。

## 5. 状态接口更新（`/api/state`）

遥测 JSON 新增字段：

- `relay3`：CH3 当前状态（0/1）
- `relay4`：CH4 当前状态（0/1）
- `relay5`：CH5 当前状态（0/1）
- `relay6`：CH6 当前状态（0/1）

原有 `relay1/relay2` 保持不变。

## 6. 控制命令更新（`POST /api/cmd`）

除原有闸门命令外，新增信号灯/蜂鸣器命令：

1. `signal_red_toggle`
2. `signal_yellow_toggle`
3. `signal_green_toggle`
4. `signal_buzzer_toggle`
5. `signal_red_on`
6. `signal_red_off`
7. `signal_yellow_on`
8. `signal_yellow_off`
9. `signal_green_on`
10. `signal_green_off`
11. `signal_buzzer_on`
12. `signal_buzzer_off`
13. `signal_all_off`

示例：

```json
{"cmd":"signal_red_toggle"}
```

```json
{"cmd":"signal_all_off"}
```

## 7. 旧接口兼容

以下兼容路由仍可用：

- `GET /Switch3` -> CH3 toggle
- `GET /Switch4` -> CH4 toggle
- `GET /Switch5` -> CH5 toggle
- `GET /Switch6` -> CH6 toggle

页面在新命令不可用时会自动回退到兼容路由（toggle 类命令）。

兼容策略说明：

- CH3-CH6 的手动命令和旧路由仍可调用。
- 但自动状态机每轮 `loop()` 会按规则重写 CH3-CH6，手动状态可能被快速覆盖（设计使然）。

## 8. 关键代码位置

- 命令与状态拼装：`src/WS_MQTT.cpp`
- 继电器执行逻辑：`src/MAIN_ALL.ino`（`Relay_Analysis` + `SignalTower_Loop`）
- GPIO 定义：`src/WS_GPIO.h`
- 本地面板：`data/ui/index.html`

## 9. 构建与烧录

在 `firmware/` 目录：

```bash
pio run
pio run -t upload
pio run -t uploadfs
```

建议：

- 若修改了 `data/ui/*`，优先执行 `uploadfs`。
- 项目启用了 UI 资源内嵌兜底（`scripts/embed_ui_assets.py`），LittleFS 缺失时可回退。
