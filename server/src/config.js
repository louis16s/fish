const { z } = require('zod');

function envBool(v, def) {
  if (v == null || v === '') return def;
  const s = String(v).trim().toLowerCase();
  if (['1', 'true', 'yes', 'y', 'on'].includes(s)) return true;
  if (['0', 'false', 'no', 'n', 'off'].includes(s)) return false;
  return def;
}

const schema = z.object({
  NODE_ENV: z.string().default('production'),
  PORT: z.coerce.number().int().positive().default(8080),
  TRUST_PROXY: z.coerce.number().int().min(0).max(5).default(1),
  SESSION_SECRET: z.string().min(16),
  COOKIE_SECURE: z.string().optional(),

  ADMIN_USERNAME: z.string().min(1).default('admin'),
  ADMIN_PASSWORD: z.string().min(8),

  DATABASE_URL: z.string().min(1),

  MQTT_URL: z.string().min(1),
  MQTT_USERNAME: z.string().optional(),
  MQTT_PASSWORD: z.string().optional(),
  MQTT_TELEMETRY_SUB: z.string().min(1).default('+/device/telemetry'),
  MQTT_REPLY_SUB: z.string().min(1).default('+/device/reply'),
  MQTT_LOG_SUB: z.string().min(1).default('+/device/log/#'),
  DEFAULT_DEVICE_ID: z.string().min(1).default('fish1'),

  DATA_RETENTION_DAYS: z.coerce.number().int().min(1).max(3650).default(30),
  HISTORY_MAX_POINTS: z.coerce.number().int().min(100).max(5000).default(1200),

  // In-memory log cache for pushed logs. Keeps the cloud panel responsive even when device RPC is slow/unavailable.
  LOG_CACHE_MAX_BYTES: z.coerce.number().int().min(16 * 1024).max(2 * 1024 * 1024).default(256 * 1024)
});

function loadConfig(processEnv) {
  const cfg = schema.parse(processEnv);
  return {
    ...cfg,
    cookieSecure: envBool(cfg.COOKIE_SECURE, true)
  };
}

module.exports = { loadConfig };
