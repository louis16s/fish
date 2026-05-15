# Fish Cloud Panel Server

`server/` 是本项目的云端控制面板服务，负责把 Web/小程序请求与设备 MQTT 通道隔离开。

核心职责：

1. 用户登录鉴权与会话管理（PostgreSQL 持久化 Session）。
2. 订阅设备遥测并写入 PostgreSQL。
3. 提供实时状态、历史查询、回放、规则读写、日志读写 API。
4. 通过 MQTT 下发控制命令与 RPC（配置/日志）。
5. 管理员功能（保留天数、用户管理）。

## 1. 技术栈

- Node.js `20+`（Dockerfile 基于 `node:20-alpine`）
- Express 4
- MQTT.js 5
- PostgreSQL (`pg`)
- `express-session` + `connect-pg-simple`
- `zod`（配置与输入校验）
- `helmet` + `express-rate-limit`

## 2. 目录结构

- `src/index.js`：主程序、路由、鉴权、MQTT RPC 协调
- `src/config.js`：环境变量解析与校验
- `src/db.js`：数据库初始化与数据访问
- `src/mqtt.js`：MQTT 连接、主题分发、RPC 关联
- `src/auth.js`：密码哈希与鉴权中间件
- `src/log_cache.js`：设备日志推送内存缓存
- `public/`：Web 页面静态资源
- `.env.example`：环境变量模板

## 3. 快速启动

### 3.1 安装

```bash
cd server
npm install
```

### 3.2 配置 `.env`

```bash
copy .env.example .env
```

必须项：

- `SESSION_SECRET`
- `ADMIN_PASSWORD`
- `DATABASE_URL`
- `MQTT_URL`
- `MQTT_USERNAME`
- `MQTT_PASSWORD`

建议项：

- `COOKIE_SECURE=1`（HTTPS 反代场景）
- `MQTT_TELEMETRY_SUB=+/device/telemetry`
- `MQTT_REPLY_SUB=+/device/reply`
- `MQTT_LOG_SUB=+/device/log/#`

### 3.3 运行

```bash
npm run start
```

开发模式（当前与 start 等价）：

```bash
npm run dev
```

### 3.4 健康检查

```bash
curl http://127.0.0.1:8080/healthz
```

正常返回示例：

```json
{
  "ok": true,
  "status": "ok",
  "service": "fish-cloud-panel",
  "checks": {
    "db": { "ok": true, "status": "ok", "latency_ms": 1 },
    "mqtt": { "ok": true, "status": "ok", "connected": true }
  }
}
```

## 4. 数据库结构

服务启动会自动 `CREATE TABLE IF NOT EXISTS`：

1. `telemetry`
- 设备遥测明细（`payload JSONB`）
- 索引：`telemetry_device_ts_idx(device_id, ts desc)`

2. `devices`
- 设备注册与最近在线时间（`last_seen_at`）

3. `users`
- 后台用户（`role: admin|user`，`disabled`）

4. `settings`
- 系统设置（当前使用 `retention_days`）

5. `user_sessions`
- 由 `connect-pg-simple` 自动维护

## 5. 启动行为与后台任务

1. 自动初始化表结构。
2. 若无可用 admin，会使用 `.env` 的 `ADMIN_USERNAME/ADMIN_PASSWORD` 引导创建 admin。
3. 订阅 MQTT 主题：`telemetry`、`reply`、`log`。
4. 将 telemetry 写入 DB 并更新设备 `last_seen_at`。
5. 每 30 分钟按 `retention_days` 清理历史 telemetry。

## 6. MQTT 约定

### 6.1 Topic

- 设备上行遥测：`<device_id>/device/telemetry`
- 设备上行 RPC 回复：`<device_id>/device/reply`
- 设备上行日志推送：`<device_id>/device/log/<name>`
- 服务器下行：`<device_id>/device/command`

### 6.2 RPC 机制

1. 服务器下发命令时自动附带 `req_id`。
2. 设备回复同 `req_id`。
3. 服务端按 `req_id` 匹配 pending 请求。
4. 超时返回 `timeout`（通常映射为 HTTP 504）。

## 7. API 总览

所有 `/api/*` 默认要求登录会话（Cookie: `fish_sid`）。

### 7.1 认证

1. `POST /api/auth/login`
- body: `{ "username": "...", "password": "..." }`
- 成功：`{ ok: true }`
- 失败：`400 missing_credentials` / `401 bad_credentials`

2. `POST /api/auth/logout`
- 销毁会话并清 Cookie

3. `GET /api/auth/me`
- 返回当前登录用户

### 7.2 页面与健康

1. `GET /healthz`
2. `GET /login`
3. `GET /`
4. `GET /config`
5. `GET /rules`
6. `GET /device-config`（302 到 `/rules`）
7. `GET /replay`
8. `GET /ui/*`（静态资源）

### 7.3 设备状态与控制

1. `GET /api/state`
- query: `device_id`（可选，默认 `DEFAULT_DEVICE_ID`）
- 返回：`mqtt_connected`、`last_telemetry_at`、`telemetry`

2. `GET /getData`
- 返回 telemetry payload，兼容设备本地面板数据结构

3. `GET /get`
- 诊断接口（UI 版本、MQTT 状态、设备在线判断等）

4. `POST /api/cmd`
- body: `{ "cmd": "gate_open|gate_close|gate_stop|auto_on|auto_off|auto_latch_off|manual_end" }`
- 权限：已登录用户
- 错误：`400 bad_cmd`、`500 mqtt_publish_failed`

### 7.4 历史与回放

1. `GET /api/history`
- query: `device_id`、`window_s`、`max_points`
- 服务端自动下采样，返回 `points[]`

2. `GET /api/telemetry/range`
- query: `device_id`、`from`、`to`、`limit`
- 返回原始 payload 明细列表

3. `GET /api/devices`
- 返回设备列表（按最近活跃排序）
- 设备可通过 MQTT 首次上报自动入库，也可由管理员手动添加

### 7.5 规则配置（MQTT RPC）

1. `GET /api/config`
- query: `device_id`、`source`
- `source=cache`：只读缓存，不走 RPC
- 响应头：`X-Config-Source: cache|rpc|cache-miss`

2. `POST /api/config`
- body: JSON（服务端会序列化后发给设备）
- 权限：仅 `admin`
- 错误：`400 invalid_json`、`504 timeout`、`502 mqtt_not_connected`

### 7.6 日志（推送缓存优先，RPC 兜底）

1. `GET /api/log`
- query: `device_id`、`name=error|measure|action`、`tail`、`bak`、`source`
- 响应头：`X-Log-Source: cache|rpc|cache-miss`

2. `POST /api/log/clear`
- body: `{ "name": "error|measure|action" }`
- 权限：仅 `admin`

3. `GET /api/log/download`
- 与 `/api/log` 参数一致
- 权限：仅 `admin`
- 强制下载文本文件

### 7.7 管理员 API

都要求 admin：

1. `GET /api/admin/settings`
2. `POST /api/admin/settings`（`retention_days`）
3. `POST /api/admin/devices`（手动新增 MQTT 设备）
字段：
- 必填：`device_id`（设备唯一 ID，topic 前缀）
- 可选：`display_name`、`mqtt_username`
- 可选 topic：`mqtt_telemetry_topic`、`mqtt_command_topic`、`mqtt_reply_topic`、`mqtt_log_topic`
- 若 topic 留空，默认：
  - `<device_id>/device/telemetry`
  - `<device_id>/device/command`
  - `<device_id>/device/reply`
  - `<device_id>/device/log`
4. `POST /api/admin/devices/:deviceId/delete`（删除设备，`fish1` 禁止删除）
5. `GET /api/admin/users`
6. `POST /api/admin/users`
7. `POST /api/admin/users/:id/password`
8. `POST /api/admin/users/:id/disable`（`admin` 用户不可禁用）
9. `POST /api/admin/users/:id/delete`（`admin` 用户不可删除）
10. `GET /api/admin/login-records`（登录审计记录）

约束补充：

- 用户名大小写不敏感唯一（`admin` 与 `Admin` 视为重复）。
- `ADMIN_USERNAME`（默认 `admin`）在服务端也会被强制保护，不能禁用/删除。

## 8. 访问控制与安全设计

1. `helmet` 安全头与受限 CSP（当前页面仍使用少量内联脚本/事件，因此保留必要兼容项）。
2. 关键操作限流：
- 登录：`20/min`
- 控制命令：`60/min`
- 管理接口：`120/min`

3. Session Cookie：
- `httpOnly`
- `sameSite=lax`
- `secure` 受 `COOKIE_SECURE` 控制
- 默认 7 天

4. Same-Origin 校验
- 变更类接口要求 `Origin/Referer` 与 `Host` 一致。
- 微信小程序请求允许 `Referer: https://servicewechat.com/<WECHAT_APPID>/...`，其他 AppID 仍拒绝。

5. 登录成功后 `session.regenerate()`，防会话固定。

## 9. 返回码与排错建议

1. `401 unauthorized`
- 未登录或会话失效。

2. `403 forbidden / bad_origin`
- 非 admin 或跨源写操作被拦截。

3. `502 mqtt_not_connected`
- 服务端未连上 Broker。

4. `504 timeout`
- MQTT RPC 超时（设备离线或链路差）。

5. `503 cache_miss`
- 请求显式 `source=cache`，但缓存无数据。

## 10. Docker 运行

本目录提供 `Dockerfile`：

```bash
docker build -t fish-cloud-panel:latest server
docker run --rm -p 8080:8080 --env-file server/.env fish-cloud-panel:latest
```

生产推荐直接参考仓库的 `openclaw/openclaw_read.md` 与 `openclaw/docker-compose.yml`，统一编排 `EMQX + PostgreSQL + Server + Caddy`。

## 11. 与小程序联调

1. 小程序 `BASE_URL` 指向本服务 HTTPS 地址。
2. 微信后台配置 request 合法域名。
3. 登录后先检查：
- `/healthz` 是否 `checks.mqtt.ok=true`、`checks.db.ok=true`
- `/api/devices` 是否有设备
- `/api/state?device_id=...` 是否有实时 telemetry

## 12. 部署验收与回滚

1. 最小验收（生产）
- [ ] `GET /healthz`：`ok=true`，且 `checks.mqtt.ok=true`、`checks.db.ok=true`
- [ ] 登录成功后 `GET /api/auth/me` 返回当前用户
- [ ] `GET /api/state` 有实时 telemetry
- [ ] `POST /api/cmd` 下发后设备侧有动作/回执
- [ ] `GET /api/history` 可返回窗口数据
- [ ] 管理员可进入 `/config` 并读取用户列表

2. 安全验收
- [ ] `COOKIE_SECURE=1`（HTTPS 下）
- [ ] 非同源写请求被 `bad_origin` 拦截（预期）
- [ ] admin 账号不可被禁用/删除（预期）

3. 快速排错命令

```bash
docker compose logs -f fish-panel
curl -i https://你的域名/healthz
curl -i https://你的域名/api/auth/me
```

4. 回滚建议
- 回滚镜像：切回上一版 `fish-panel` 镜像并 `docker compose up -d`
- 数据保留：不要删除 `pgdata` 卷，避免历史数据丢失
- 若仅配置错误：优先修复 `.env` 并重启容器，不要直接清库
