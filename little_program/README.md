# Fish Cloud Panel Mini Program (WeChat)

`little_program/` 是本项目的微信小程序客户端，通过 `server/` 提供的 HTTPS API 完成登录、控制、配置、回放与管理。

## 1. 功能概览

1. 登录鉴权
- `POST /api/auth/login`
- `GET /api/auth/me`
- `POST /api/auth/logout`

2. 设备操作
- 设备列表：`GET /api/devices`
- 实时状态：`GET /api/state`
- 控制命令：`POST /api/cmd`

3. 配置与日志
- 规则读取：`GET /api/config`
- 规则保存：`POST /api/config`
- 日志读取：`GET /api/log`
- 日志清空：`POST /api/log/clear`

4. 历史回放
- 窗口历史：`GET /api/history`
- 时间范围明细：`GET /api/telemetry/range`
- 导出并发送 JSON/CSV 文件（依赖 `wx.shareFileMessage`）

5. 管理员功能
- 系统设置：`GET/POST /api/admin/settings`
- 用户管理：`GET/POST /api/admin/users`、`/api/admin/users/:id/*`

## 2. 目录结构

- `app.js`：全局状态（当前用户、当前设备）
- `app.json`：页面路由
- `utils/config.js`：服务端地址配置
- `utils/api.js`：请求封装 + Cookie 合并
- `utils/format.js`：时间/状态格式化
- `pages/login`：登录页
- `pages/panel`：主控面板（实时状态、命令、日志、策略摘要、动态示意图）
- `pages/device-config`：规则配置编辑器
- `pages/replay`：历史曲线 + 范围查询 + 导出
- `pages/admin`：管理员页

## 3. 运行前配置

### 3.1 配置 API 地址

编辑 `little_program/utils/config.js`：

```js
const BASE_URL = 'https://你的域名';
const RAW_BASE_URL = '';
```

说明：

- 当前实现仅使用 `BASE_URL`。
- 必须是 HTTPS（微信 request 合法域名要求）。

### 3.2 微信后台域名白名单

微信公众平台 -> 开发管理 -> 开发设置：

- 添加 `request 合法域名`：`https://你的域名`

### 3.3 导入项目

微信开发者工具：

- 项目目录：`D:\users\code\fish-github\little_program`
- AppID：使用你自己的小程序 AppID 或测试号

## 4. 服务端前置条件

1. `server` 已部署并可通过 HTTPS 访问。
2. `server/.env` 至少配置：
- `SESSION_SECRET`
- `ADMIN_USERNAME`
- `ADMIN_PASSWORD`
- `DATABASE_URL`
- `MQTT_URL`

3. `GET /healthz` 返回 `ok=true`。
4. 设备已上报 telemetry（否则设备列表为空）。

## 5. 页面行为说明

## 5.1 登录页 `pages/login`

1. 支持本地“记住密码”缓存：
- `remember_password`
- `remember_username`
- `remember_password_value`

2. 页面打开时：
- 若勾选记住且账号密码存在，会尝试自动登录。

3. 登录成功后：
- 拉取 `/api/auth/me`
- 跳转 `pages/panel`

## 5.2 主控页 `pages/panel`

轮询策略：

- 状态轮询 `STATE_POLL_MS = 2500ms`
- 日志轮询 `LOG_POLL_MS = 10000ms`

控制命令：

- `gate_open`
- `gate_close`
- `gate_stop`
- `auto_on`
- `auto_off`
- `auto_latch_off`
- `manual_end`

关键行为：

1. `auto_latch_off` 状态下执行开/关/停时，会尝试再次补发 `auto_latch_off`，尽量保持“锁定关闭自动”语义。
2. 日志优先读取缓存：先请求 `source=cache`，miss 后回退 RPC。
3. 页面下方闸门示意图为 Canvas 动画，不依赖远端 SVG。

设备在线判定：

- 使用 `last_telemetry_at` 与本地时间比较，阈值 15 秒。

## 5.3 设备配置页 `pages/device-config`

支持两种操作方式：

1. 可视化编辑（推荐）
- 模式：`mixed/daily/cycle/leveldiff`
- 时区：`tz_offset_ms`
- daily/cycle/leveldiff 多组规则编辑

2. JSON 原文编辑
- 可将 JSON 应用到表单
- 可从表单重建 JSON

约束（与页面实现一致）：

- daily 最多 8 组
- cycle 最多 5 组，每组最多 10 段
- leveldiff 最多 4 组
- cycle 同时最多 1 组启用
- leveldiff 同时最多 1 组启用

读取策略：

- 优先 `GET /api/config?source=cache`
- 缓存 miss 后再请求 `GET /api/config`

## 5.4 回放页 `pages/replay`

1. 范围查询
- `GET /api/telemetry/range`
- 参数：`from`、`to`、`limit`

2. 窗口历史
- `GET /api/history`
- 参数：`window_s`、`max_points`

3. 绘图
- 使用 Canvas 绘制内塘/外塘曲线
- 动态计算 Y 轴范围，支持点击点位查看明细

4. 导出
- 可导出 JSON 或 CSV
- 写入 `wx.env.USER_DATA_PATH`
- 调用 `wx.shareFileMessage` 发送文件
- 本地空间不足时会自动清理历史 `replay_*.json/csv` 与 `tmp_*.json/csv` 后重试

## 5.5 管理页 `pages/admin`

1. 状态检查
- 调 `GET /healthz` 展示 MQTT/DB 状态

2. 数据保留天数
- `GET /api/admin/settings`
- `POST /api/admin/settings`

3. 用户管理
- 列表：`GET /api/admin/users`
- 创建：`POST /api/admin/users`
- 禁用/启用：`POST /api/admin/users/:id/disable`
- 重置密码：`POST /api/admin/users/:id/password`

4. 设备信息
- 先 `GET /api/devices`
- 再逐个请求 `/api/state` 补充固件版本、RSSI、LAN IP

## 6. 设备选择与全局状态

全局状态在 `app.js`：

- `globalData.user`
- `globalData.currentDeviceId`
- `globalData.devices`

持久化行为：

1. 当前设备 ID 会存入 `wx.setStorageSync('current_device_id', ...)`。
2. App 启动时读取该值恢复默认设备。
3. 所有业务请求统一通过 query `device_id` 传递目标设备。

## 7. 请求与会话机制

`utils/api.js` 行为：

1. 自动拼接 `BASE_URL + path`。
2. 自动提取 `Set-Cookie` 并合并到后续 `Cookie` 请求头。
3. 提供：
- `requestJSON`
- `requestText`
- `buildQuery`

错误语义：

- 非 2xx 会抛出 Error，`message` 优先取服务端 `error` 字段。

## 8. 常见问题

1. 登录失败 `401 bad_credentials`
- 用户名或密码错误。
- 或服务端尚未初始化 admin（检查 `server` 日志与 `.env`）。

2. 控制返回 `502 mqtt_not_connected`
- 服务端未连接 MQTT Broker。

3. 控制返回 `504 timeout`
- 设备离线、信号差或 RPC 未及时响应。

4. 设备列表为空
- 设备未上报 telemetry 到云端。

5. 导出失败（空间不足）
- 缩小查询时间范围或 `limit`。
- 让页面自动清理旧导出后重试。

6. 管理接口 `403 forbidden`
- 当前账号不是 admin。

## 9. 联调建议

1. 先在浏览器验证服务端接口（登录、设备列表、状态）。
2. 再在小程序接入 `BASE_URL`。
3. 先跑主控页，再测配置页/回放页/管理页。
4. 若出现间歇超时，优先检查 MQTT 与设备在线状态。

## 10. 相关文档

- 根目录说明：`../README.md`
- 服务端文档：`../server/README.md`
