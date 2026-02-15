# OpenClaw 部署说明（EMQX + 外网面板 + 历史回放）

目标：把该 ESP32-S3 双水位计水闸控制设备的 MQTT 上云、历史数据落库、外网安全面板（带登录/用户管理/数据回放）部署到 `fish.530555.xyz`。

本仓库已提供一个可直接落地的最小可用方案：

- MQTT Broker：EMQX（容器）
- 数据库：PostgreSQL（容器）
- 外网面板：`server/`（Node.js，容器）
- HTTPS 反向代理：Caddy（容器，自动签发证书）

文件在：

- `openclaw/docker-compose.yml`
- `openclaw/.env.example`
- `openclaw/caddy/Caddyfile`
- `openclaw/emqx/acl.conf`（ACL 示例）
- 面板代码：`server/`（含 `server/public/*` 前端）

---

## 1. 主题与数据格式（设备侧）

设备固件配置在 `src/WS_Information.h`（你本地已经有）：

- 遥测上报主题（设备 -> MQTT）：`MQTT_Pub`，默认 `fish1/device/telemetry`
- 控制下发主题（MQTT -> 设备）：`MQTT_Sub`，默认 `fish1/device/command`
- 遥测内容：与设备 HTTP 的 `GET /getData` 基本一致（详见仓库 `README.md` 的示例 JSON）
- 下发命令（推荐格式）：
  - `{"cmd":"gate_open"}`
  - `{"cmd":"gate_close"}`
  - `{"cmd":"gate_stop"}`
  - `{"cmd":"auto_on"}`
  - `{"cmd":"auto_off"}`
  - `{"cmd":"auto_latch_off"}`
  - `{"cmd":"manual_end"}`

面板服务端会订阅遥测主题，把每条遥测写入数据库，并缓存最新一条用于 `/api/state`。

---

## 2. 安全部署原则（强烈建议）

1. **不要把 MQTT 密码放到浏览器端**
   - 本方案的 Web UI 只访问 HTTPS 的 Web API；MQTT 账号仅保存在服务器环境变量中。

2. **EMQX 禁止匿名访问 + 强口令 + ACL**
   - 至少做到：`allow_anonymous=false`（或等价设置）
   - 给“设备”和“服务器”使用不同账号
   - ACL 最小权限：
     - 设备账号：只能 `publish fish1/device/telemetry`，只能 `subscribe fish1/device/command`
     - 服务器账号：只能 `subscribe +/device/telemetry`，只能 `publish +/device/command`
   - 示例 ACL 见 `openclaw/emqx/acl.conf`（如你的 EMQX 版本不吃该文件，请在 Dashboard 里做同样的授权规则）

3. **面板必须 HTTPS**
   - Caddy 自动签发并续期证书；浏览器 Cookie 使用 `httpOnly + secure`（默认开启）。

4. **EMQX Dashboard 不要直接暴露公网**
   - 如果需要远程访问，建议：
     - 仅内网/SSH 隧道访问
     - 或通过反代加二次认证（basic auth / IP allowlist / VPN）

5. **开端口最小化**
   - 必须：`80/443`（面板）
   - MQTT（当前固件无 TLS）：需要 `1883` 公网可达
   - 如果后续你升级固件支持 `mqtts`：建议改为只开放 `8883` 并关闭 `1883`

---

## 3. OpenClaw 一键部署（docker compose）

### 3.1 准备

1. 确认 DNS 已指向该 VPS（你已配置域名）：`fish.530555.xyz`
2. VPS 需要安装：
   - Docker
   - Docker Compose（v2）

### 3.2 配置环境变量

在 `openclaw/` 下创建 `.env`（参考 `openclaw/.env.example`）：

- `POSTGRES_PASSWORD`：数据库强密码
- `SESSION_SECRET`：面板会话签名密钥（很长的随机字符串）
- `ADMIN_USERNAME` / `ADMIN_PASSWORD`：首个管理员账号（只在“没有任何 admin”时自动创建）
- `MQTT_SERVER_USERNAME` / `MQTT_SERVER_PASSWORD`：面板服务端连接 EMQX 的账号
- `MQTT_TELEMETRY_SUB`：建议 `+/device/telemetry`（支持多设备）
- `DEFAULT_DEVICE_ID`：单设备时可写 `fish1`
- `DATA_RETENTION_DAYS`：历史数据保留天数

### 3.3 启动

在 VPS 上：

```bash
cd openclaw
docker compose --env-file .env up -d --build
```

启动后访问：

- 外网面板：`https://fish.530555.xyz/`
- 配置页：`https://fish.530555.xyz/config`
- 回放页：`https://fish.530555.xyz/replay`

---

## 4. EMQX 侧需要做什么（最少步骤）

如果你用本方案的 `emqx` 容器：

### 4.1 推荐做法：用 EMQX Dashboard 配置（不暴露公网）

1. 临时开启 Dashboard（仅本机访问，不暴露公网）
   - 在 `openclaw/docker-compose.yml` 的 `emqx.ports` 里取消注释（或添加）一行：
     - `127.0.0.1:18083:18083`
   - 然后重启：`docker compose up -d`
   - 如果你是远程访问 Dashboard，建议用 SSH 隧道：
     - `ssh -L 18083:127.0.0.1:18083 root@<你的VPS>`
     - 本机浏览器访问：`http://127.0.0.1:18083`

2. 修改 Dashboard 默认管理员密码
   - EMQX 默认 Dashboard 账号通常是 `admin/public`（不同镜像/版本可能不同）
   - 登录后第一时间修改为强密码

3. 禁止匿名连接（必须）
   - 确保 `allow_anonymous=false`（或在 Dashboard 里关闭匿名访问/启用认证链）

4. 创建两个 MQTT 用户（设备账号 + 服务器账号）
   - 设备用户：例如 `fish1`（固件里的 `MQTT_Username`）
   - 服务器用户：例如 `fish_srv`（`openclaw/.env` 的 `MQTT_SERVER_USERNAME`）

5. 配置授权（ACL，最小权限原则）
   - `fish1`：
     - allow publish: `fish1/device/telemetry`
     - allow publish: `fish1/device/reply`
     - allow subscribe: `fish1/device/command`
   - `fish_srv`：
     - allow subscribe: `+/device/telemetry`
     - allow subscribe: `+/device/reply`
     - allow publish: `+/device/command`

6. 配置完成后，建议把 `openclaw/docker-compose.yml` 里的 Dashboard 端口映射删掉/注释掉（保持不对公网开放）。

### 4.2 备选：文件 ACL（仅作为参考）

- `openclaw/emqx/acl.conf` 提供了一个 ACL 文件示例（不同 EMQX 版本启用方式可能不同）。
- 如果你的 EMQX 版本不使用该文件，请以 Dashboard 中配置的认证/授权规则为准。

### 4.3 MQTT/EMQX 配置清单（交付 OpenClaw）

必须项（强烈建议都完成）：

- [ ] MQTT 监听端口：`1883/tcp` 对公网开放（当前固件使用 `mqtt://`，不支持 `mqtts://`）。
- [ ] EMQX Dashboard：默认不暴露公网；如需远程配置，用 `127.0.0.1:18083:18083` + SSH 隧道，配置完关闭端口映射。
- [ ] 关闭匿名连接：所有客户端都必须认证（等价目标：`allow_anonymous=false`）。
- [ ] 创建 MQTT 用户（建议至少 2 个账号，分权）：
  - 设备用户：`fish1`（或你的 `device_id`），强密码
  - 服务器用户：`fish_srv`，强密码（供 `server/` 外网面板连接 EMQX）
- [ ] 配置授权/ACL（默认 `deny`，最小权限原则）：
  - 设备用户 `fish1`：
    - allow publish：`fish1/device/telemetry`
    - allow publish：`fish1/device/reply`
    - allow subscribe：`fish1/device/command`
  - 服务器用户 `fish_srv`：
    - allow subscribe：`+/device/telemetry`
    - allow subscribe：`+/device/reply`
    - allow publish：`+/device/command`
- [ ] 对齐配置：
  - `openclaw/.env` 的 `MQTT_SERVER_USERNAME/MQTT_SERVER_PASSWORD` 要与 EMQX 里创建的“服务器用户”一致
  - 设备固件 `src/WS_Information.h` 的 `MQTT_Server/MQTT_Port/MQTT_Username/MQTT_Password/MQTT_Pub/MQTT_Sub` 要与 EMQX 一致

可选增强（建议按时间做）：

- [ ] 启用 `mqtts`（`8883/tcp`）：等固件支持后再开启 TLS，并关闭 `1883`。
- [ ] 防火墙：限制 `18083`（Dashboard）仅本机/运维网段可访问。
- [ ] 限流与资源限制：连接数上限、消息大小上限（防滥用/DoS）。
- [ ] 监控与审计：关注连接断开原因、认证失败、授权拒绝事件。

配置落点与 Git 忽略：

- 版本库内：`openclaw/docker-compose.yml`、`openclaw/emqx/acl.conf`（如果使用文件 ACL）
- 持久化：Docker volume `emqx_data` / `emqx_log`（EMQX 用户/规则/配置在容器数据里持久化）
- 必须 `git ignore`：`openclaw/.env`、（若使用 TLS）`openclaw/emqx/certs/*`

---

## 5. 外网面板功能说明

面板代码在 `server/`：

- 登录：`/login`
- 主页（控制面板）：`/`
  - `GET /api/state` 获取最新遥测（结构与设备 `/api/state` 一致：包含 `telemetry` 字段）
  - `POST /api/cmd` 下发控制（服务端发布 MQTT `device_id/device/command`）
  - 历史水位：自动调用 `GET /api/history?window_s=...` 从服务器拉取历史（默认固定 0-5m，超过 5m 才自动扩展标尺）
- 设备配置：`/device-config`
  - `GET /api/config` / `POST /api/config` 通过 MQTT RPC 读写设备 `ctrl.json`
  - `GET /api/log` / `POST /api/log/clear` / `GET /api/log/download` 通过 MQTT RPC 读取/清空日志（默认读取末尾 16KB）
- 回放：`/replay`
  - 可按时间范围查询服务器保存的所有遥测并导出 JSON/CSV
- 配置：`/config`（管理员）
  - 设置数据保留天数
  - 用户管理（创建/禁用/重置密码）
  - 设备列表

---

## 6. 设备固件需要调整的点（可选）

如果你部署了新的 EMQX/域名，设备侧需要把 `src/WS_Information.h` 的：

- `MQTT_Server`
- `MQTT_Port`
- `MQTT_Username`
- `MQTT_Password`
- `MQTT_Pub`
- `MQTT_Sub`

改成与你的 EMQX 设置一致，然后重新刷固件。

---

## 7. 备份与运维建议

1. PostgreSQL 备份（每天）：对 `pgdata` 做快照或 `pg_dump`
2. 监控：
   - 看板上 `MQTT 已连接`/`DB OK`
   - EMQX 连接数/断开原因
3. 密码轮换：
   - MQTT 用户密码
   - 面板 admin 密码
   - `SESSION_SECRET`（轮换会让所有用户重新登录）
