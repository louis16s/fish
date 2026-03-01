# OpenClaw 服务器调整说明（小程序日志读取）

## 结论

1. 小程序访问的 `GET /api/log?device_id=fish1&name=error&tail=16384&source=cache` 是当前设计行为。  
2. 该接口日志来源不是 PostgreSQL；优先来源是服务端内存日志缓存（由 MQTT 日志推送填充），缓存未命中才回退设备 MQTT RPC。  
3. 我已在小程序侧降频日志轮询（从 2.5s 一次改为 10s 一次，并增加防并发），减少对 `source=cache` 的持续请求压力。

## 对 OpenClaw 的服务器侧检查项

1. 确认服务端已订阅日志主题：`MQTT_LOG_SUB=+/device/log/#`。  
2. 确认设备固件开启日志推送：`MQTT_LOG_PUSH_Enable=true`。  
3. 确认设备实际发布以下主题（纯文本 payload）：  
   - `<device_id>/device/log/error`  
   - `<device_id>/device/log/measure`  
   - `<device_id>/device/log/action`  
4. 确认 Broker ACL 允许：  
   - 设备账号：publish `fish1/device/log/#`、`fish1/device/telemetry`、`fish1/device/reply`；subscribe `fish1/device/command`  
   - 服务器账号：subscribe `+/device/log/#`、`+/device/telemetry`、`+/device/reply`；publish `+/device/command`
5. 确认服务端环境变量 `LOG_CACHE_MAX_BYTES` 足够（建议 `262144` 或更高），避免缓存过小导致频繁 miss。

## 验证方法

1. 登录后调用：`GET /api/log?device_id=fish1&name=error&tail=16384&source=cache`。  
2. 观察响应头 `X-Log-Source`：  
   - `cache`：正常命中（推荐状态）  
   - `cache-miss`：MQTT 推送链路或缓存未建立  
   - `rpc`：走了设备 RPC 回退（链路慢时可能超时）
3. 调用 `GET /healthz`，确认 `mqtt=true`、`db=true`。

## 需要 OpenClaw 调整的场景

1. `source=cache` 长期返回 `503 cache_miss`：优先排查 MQTT 日志推送主题和 ACL。  
2. `source=cache` 偶尔命中但经常 miss：增大 `LOG_CACHE_MAX_BYTES`，并检查服务端重启频率。  
3. `/api/log` 经常 504：说明回退 RPC 超时，需优先修复缓存链路，而不是提高前端重试频率。
