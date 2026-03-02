# OpenClaw Deployment Tasks

目标：由 OpenClaw 按任务单完成 `EMQX + PostgreSQL + fish-panel + Caddy` 的生产部署，并输出可复核证据。

关联文档：
- `openclaw/openclaw_read.md`（完整部署说明）
- `openclaw/docker-compose.yml`
- `openclaw/.env.example`

## 1. 服务器准备

- [ ] 准备一台 Linux VPS（公网 IP 可访问）
- [ ] 安装 Docker 与 Docker Compose v2
- [ ] 防火墙仅放行 `80/tcp`、`443/tcp`、`1883/tcp`
- [ ] 域名 `fish.530555.xyz` A 记录指向 VPS

验证命令：

```bash
docker --version
docker compose version
```

## 2. 环境变量与密钥

- [ ] 在 `openclaw/.env` 写入强密码与随机密钥（不要提交到 Git）
- [ ] 至少配置：
  - `POSTGRES_PASSWORD`
  - `SESSION_SECRET`
  - `ADMIN_USERNAME` / `ADMIN_PASSWORD`
  - `MQTT_SERVER_USERNAME` / `MQTT_SERVER_PASSWORD`
  - `DEFAULT_DEVICE_ID`
  - `DATA_RETENTION_DAYS`

验证命令：

```bash
cd openclaw
grep -E "POSTGRES_PASSWORD|SESSION_SECRET|ADMIN_PASSWORD|MQTT_SERVER_PASSWORD" .env
```

## 3. 容器编排启动

- [ ] 启动全部服务
- [ ] 确认 `postgres`、`emqx`、`fish-panel`、`caddy` 全部 `Up`

执行命令：

```bash
cd openclaw
docker compose --env-file .env up -d --build
docker compose ps
```

## 4. EMQX 安全配置

- [ ] 关闭匿名连接（等价目标：`allow_anonymous=false`）
- [ ] 创建设备 MQTT 用户（例：`fish1`）
- [ ] 创建服务端 MQTT 用户（例：`fish_srv`）
- [ ] 配置最小权限 ACL：
  - 设备用户：发布 telemetry/reply/log，订阅 command
  - 服务端用户：订阅 telemetry/reply/log，发布 command
- [ ] Dashboard 不对公网开放（仅本机或 SSH 隧道）

## 5. 面板可用性验证

- [ ] 打开 `https://fish.530555.xyz/`（证书有效）
- [ ] 使用 admin 登录成功
- [ ] `GET /healthz` 为 `ok=true,mqtt=true,db=true`
- [ ] 设备上报后，页面可看到实时状态
- [ ] 下发 `gate_open/gate_close/gate_stop` 至少各一次
- [ ] 回放页面可查询历史并导出

验证命令：

```bash
curl -sS https://fish.530555.xyz/healthz
docker compose logs --tail=200 fish-panel
docker compose logs --tail=200 emqx
```

## 6. 交付物（必须）

- [ ] 交付部署后的访问地址与管理员初始账号（密码单独通道交付）
- [ ] 交付 `.env` 的字段说明（不要明文给敏感值）
- [ ] 交付 EMQX 用户与 ACL 截图/配置清单
- [ ] 交付验收截图：
  - 登录页与主控页
  - `/healthz` 返回
  - 回放页曲线
  - `docker compose ps` 状态
- [ ] 交付回滚方案（镜像回滚 + 数据卷保留）

## 7. 失败回退

1. 若 `fish-panel` 异常
- 回滚到上一个可用镜像 tag，并 `docker compose up -d`

2. 若数据库异常
- 保留 `pgdata` 卷，先修复连接配置，再恢复服务

3. 若 EMQX 认证异常
- 优先核对账号密码与 ACL，再重启 `emqx`

禁止操作：
- 不允许删除 `pgdata`、`emqx_data` 后“重装了事”
- 不允许把 Dashboard (`18083`) 直接暴露公网
