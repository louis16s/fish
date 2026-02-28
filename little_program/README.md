# Fish Cloud Panel Mini Program (WeChat)

本目录提供了一个可直接运行的微信小程序，已覆盖 Web 外网面板核心功能：

- 登录/退出：`/api/auth/login`、`/api/auth/logout`、`/api/auth/me`
- 设备列表与切换：`/api/devices`
- 实时状态与控制：`/api/state`、`/api/cmd`
- 设备规则配置读取/保存：`/api/config`
- 日志读取与清空：`/api/log`、`/api/log/clear`
- 历史与回放：`/api/history`、`/api/telemetry/range`
- 管理员功能：`/api/admin/settings`、`/api/admin/users*`

## 目录结构

- `app.js` / `app.json` / `app.wxss`
- `utils/config.js`：小程序 API 地址
- `utils/api.js`：请求与 Cookie 会话管理
- `pages/login`：登录页
- `pages/panel`：总览、控制、日志、规则摘要、示意图
- `pages/device-config`：设备配置（JSON 编辑+保存）
- `pages/replay`：历史曲线与范围回放
- `pages/admin`：管理员设置、用户管理、设备列表

## 快速配置

1. 修改 API 地址

编辑 `little_program/utils/config.js`：

```js
const BASE_URL = 'https://你的域名';
```

要求：

- 必须是 HTTPS
- 域名能访问你的 `server/` 服务

2. 微信公众平台配置合法域名

在微信公众平台 -> 开发管理 -> 开发设置：

- 添加 `request 合法域名`：`https://你的域名`

3. 微信开发者工具导入

- 项目目录：`D:\users\code\fish-github\little_program`
- AppID：填你自己的（或测试号）
- 编译运行后先进入登录页

## 服务端前置条件

- 你已按仓库 `server/` 部署并可对外访问
- `server/.env` 中会话与管理员已配置：
  - `SESSION_SECRET`
  - `ADMIN_USERNAME`
  - `ADMIN_PASSWORD`
- MQTT / PostgreSQL 正常（可通过 `GET /healthz` 检查）

## 使用说明

1. 登录后进入“控制面板”
   - 支持“记住密码”（本地存储在小程序缓存）
2. 先选择设备，再进行控制/看状态
3. “设备配置”页可直接编辑 `ctrl.json` 并保存
4. “回放”页支持
   - 历史窗口曲线（`/api/history`）
   - 时间范围查询（`/api/telemetry/range`）
   - 复制 JSON/CSV 到剪贴板
5. “管理”页仅 admin 可完整使用

## 示意图说明

- 主面板示意图地址：`https://你的域名/ui/pond_gate.svg`
- 若示意图显示加载失败，请检查：
  - 服务器该路径是否可访问
  - 微信后台 `request 合法域名` 是否已配置该域名

## 常见问题

1. 登录 401
- 检查 `BASE_URL`
- 检查微信后台合法域名
- 检查服务端是否 HTTPS 与会话配置正常

2. 控制或保存配置失败（502/504）
- 502：MQTT 未连接
- 504：设备 MQTT RPC 超时（设备离线/链路异常）

3. 设备为空
- 设备需先上报 telemetry，才会出现在 `/api/devices`

4. 小程序里“导出文件”
- 当前实现是“复制 JSON/CSV 到剪贴板”
- 若需下载文件到本地，可后续追加 `FileSystemManager` 保存逻辑
