const path = require('path');

require('dotenv').config({ path: path.join(__dirname, '..', '.env') });

const express = require('express');
const helmet = require('helmet');
const rateLimit = require('express-rate-limit');
const session = require('express-session');
const PgSession = require('connect-pg-simple')(session);
const { z } = require('zod');

const { loadConfig } = require('./config');
const {
  makePool,
  initDb,
  getSetting,
  setSetting,
  ensureAdminUser,
  findUserByUsername,
  listUsers,
  createUser,
  setUserPassword,
  setUserDisabled,
  upsertDeviceSeen,
  listDevices,
  insertTelemetry,
  getLatestTelemetry,
  cleanupTelemetry,
  countTelemetry,
  queryHistoryDownsampled
} = require('./db');
const { createMqttClient } = require('./mqtt');
const {
  hashPassword,
  verifyPassword,
  sessionUser,
  requireAuthApi,
  requireAuthPage,
  requireAdmin
} = require('./auth');

const cfg = loadConfig(process.env);

const PUBLIC_DIR = path.join(__dirname, '..', 'public');
const UI_DIR = path.join(PUBLIC_DIR, 'ui');

function clampInt(v, a, b, def) {
  const n = parseInt(v, 10);
  if (!Number.isFinite(n)) return def;
  return Math.min(b, Math.max(a, n));
}

function parseDateInput(v) {
  if (!v) return null;
  const d = new Date(String(v));
  if (!Number.isFinite(d.getTime())) return null;
  return d;
}

function pickDeviceId(req, fallback) {
  const q = req && req.query ? req.query : {};
  const fromQuery = (q.device_id || q.device || q.dev || '').toString().trim();
  if (fromQuery) return fromQuery;
  return fallback || cfg.DEFAULT_DEVICE_ID;
}

async function main() {
  const pool = makePool(cfg.DATABASE_URL);
  await initDb(pool);

  // Settings defaults.
  const curRet = await getSetting(pool, 'retention_days');
  if (curRet == null) {
    await setSetting(pool, 'retention_days', String(cfg.DATA_RETENTION_DAYS));
  }

  // Bootstrap admin user only when there is no active admin.
  const adminHash = await hashPassword(cfg.ADMIN_PASSWORD);
  const boot = await ensureAdminUser(pool, cfg.ADMIN_USERNAME, adminHash);
  if (boot.created) {
    // eslint-disable-next-line no-console
    console.log('[boot] admin user created:', cfg.ADMIN_USERNAME);
  }

  // In-memory latest cache (fast /api/state).
  const latest = new Map(); // deviceId -> {receivedAt:number, payload:object}

  const mqtt = createMqttClient(cfg, {
    onTelemetry: async ({ deviceId, topic, payload, receivedAt }) => {
      try {
        if (!deviceId) return;
        latest.set(deviceId, { receivedAt, payload: payload || {} });
        await upsertDeviceSeen(pool, deviceId, new Date(receivedAt));
        await insertTelemetry(pool, deviceId, receivedAt, payload, topic);
      } catch (e) {
        // eslint-disable-next-line no-console
        console.error('[mqtt] store failed:', e && e.message ? e.message : e);
      }
    },
    onBadMessage: (topic, err) => {
      // eslint-disable-next-line no-console
      console.warn('[mqtt] bad json:', topic, err && err.message ? err.message : err);
    }
  });

  // Retention cleanup: run every 30 minutes.
  setInterval(async () => {
    try {
      const v = await getSetting(pool, 'retention_days');
      const days = clampInt(v, 1, 3650, cfg.DATA_RETENTION_DAYS);
      await cleanupTelemetry(pool, days);
    } catch (e) {
      // eslint-disable-next-line no-console
      console.warn('[cleanup] failed:', e && e.message ? e.message : e);
    }
  }, 30 * 60 * 1000);

  const app = express();
  app.disable('x-powered-by');
  app.set('trust proxy', cfg.TRUST_PROXY);

  // Security headers. (We keep CSP off because UI uses inline <script>/<style>.)
  app.use(helmet({ contentSecurityPolicy: false }));

  app.use(express.json({ limit: '1mb' }));

  app.use(
    session({
      store: new PgSession({
        pool,
        tableName: 'user_sessions',
        createTableIfMissing: true
      }),
      name: 'fish_sid',
      secret: cfg.SESSION_SECRET,
      resave: false,
      saveUninitialized: false,
      cookie: {
        httpOnly: true,
        secure: !!cfg.cookieSecure,
        sameSite: 'lax',
        maxAge: 7 * 24 * 3600 * 1000
      }
    })
  );

  // --- Health ---
  app.get('/healthz', async (req, res) => {
    try {
      await pool.query('SELECT 1');
      res.json({ ok: true, mqtt: !!mqtt.state.connected, db: true });
    } catch (e) {
      res.status(500).json({ ok: false, mqtt: !!mqtt.state.connected, db: false });
    }
  });

  // --- Static Assets ---
  app.use('/ui', express.static(UI_DIR, { fallthrough: false, maxAge: '7d' }));

  // --- Pages ---
  app.get('/login', (req, res) => {
    if (sessionUser(req)) return res.redirect('/');
    res.setHeader('Cache-Control', 'no-store');
    res.sendFile(path.join(PUBLIC_DIR, 'login.html'));
  });
  app.get('/', requireAuthPage, (req, res) => {
    res.setHeader('Cache-Control', 'no-store');
    res.sendFile(path.join(PUBLIC_DIR, 'index.html'));
  });
  app.get('/config', requireAuthPage, (req, res) => {
    res.setHeader('Cache-Control', 'no-store');
    res.sendFile(path.join(PUBLIC_DIR, 'config.html'));
  });
  app.get('/replay', requireAuthPage, (req, res) => {
    res.setHeader('Cache-Control', 'no-store');
    res.sendFile(path.join(PUBLIC_DIR, 'replay.html'));
  });

  // --- Auth APIs ---
  const loginLimiter = rateLimit({
    windowMs: 60 * 1000,
    max: 20,
    standardHeaders: true,
    legacyHeaders: false
  });

  app.post('/api/auth/login', loginLimiter, async (req, res) => {
    const body = (req && req.body) ? req.body : {};
    const username = (body.username || '').toString().trim();
    const password = (body.password || '').toString();
    if (!username || !password) return res.status(400).json({ ok: false, error: 'missing_credentials' });

    const u = await findUserByUsername(pool, username);
    if (!u || u.disabled) return res.status(401).json({ ok: false, error: 'bad_credentials' });

    const ok = await verifyPassword(password, u.password_hash);
    if (!ok) return res.status(401).json({ ok: false, error: 'bad_credentials' });

    req.session.user = { id: u.id, username: u.username, role: u.role };
    res.json({ ok: true });
  });

  app.post('/api/auth/logout', (req, res) => {
    try {
      req.session.destroy(() => {
        res.json({ ok: true });
      });
    } catch (e) {
      res.json({ ok: true });
    }
  });

  app.get('/api/auth/me', requireAuthApi, (req, res) => {
    res.json({ ok: true, user: sessionUser(req) });
  });

  // --- Device APIs (compat with ESP32 UI) ---
  app.get('/api/state', requireAuthApi, async (req, res) => {
    const deviceId = pickDeviceId(req);
    const cur = latest.get(deviceId);
    let tele = cur ? cur.payload : null;
    let lastAt = cur ? cur.receivedAt : 0;
    if (!tele) {
      const row = await getLatestTelemetry(pool, deviceId);
      if (row && row.payload) tele = row.payload;
      if (row && row.ts) lastAt = new Date(row.ts).getTime();
    }
    res.setHeader('Cache-Control', 'no-store');
    res.json({
      mqtt_connected: !!mqtt.state.connected,
      last_telemetry_at: lastAt || 0,
      device_id: deviceId,
      telemetry: tele || {}
    });
  });

  app.get('/getData', requireAuthApi, async (req, res) => {
    const deviceId = pickDeviceId(req);
    const cur = latest.get(deviceId);
    let tele = cur ? cur.payload : null;
    if (!tele) {
      const row = await getLatestTelemetry(pool, deviceId);
      if (row && row.payload) tele = row.payload;
    }
    res.setHeader('Cache-Control', 'no-store');
    res.json(tele || {});
  });

  const cmdSchema = z.object({ cmd: z.string().min(1) });
  const allowedCmd = new Set([
    'gate_open',
    'gate_close',
    'gate_stop',
    'auto_on',
    'auto_off',
    'auto_latch_off',
    'manual_end'
  ]);

  app.post('/api/cmd', requireAuthApi, async (req, res) => {
    let cmd = '';
    try {
      const body = cmdSchema.parse(req.body || {});
      cmd = body.cmd;
    } catch (e) {
      cmd = (req.query && req.query.cmd) ? String(req.query.cmd) : '';
    }
    cmd = String(cmd || '').trim();
    if (!allowedCmd.has(cmd)) return res.status(400).json({ ok: false, error: 'bad_cmd' });

    const deviceId = pickDeviceId(req);
    try {
      await mqtt.publishCommand(deviceId, cmd);
      res.json({ ok: true });
    } catch (e) {
      res.status(500).json({ ok: false, error: 'mqtt_publish_failed' });
    }
  });

  // --- History / Replay ---
  app.get('/api/history', requireAuthApi, async (req, res) => {
    const deviceId = pickDeviceId(req);
    const windowS = clampInt(req.query.window_s, 60, 30 * 24 * 3600, 60 * 60);
    const end = new Date();
    const from = new Date(end.getTime() - windowS * 1000);

    const maxPoints = clampInt(req.query.max_points, 100, 5000, cfg.HISTORY_MAX_POINTS);
    const total = await countTelemetry(pool, deviceId, from, end);
    const stride = Math.max(1, Math.ceil(total / Math.max(1, maxPoints)));
    const rows = await queryHistoryDownsampled(pool, deviceId, from, end, stride);
    const points = rows.map((r) => ({
      ts_s: Number(r.ts_s || 0),
      inner_mm: (typeof r.inner_mm === 'number') ? r.inner_mm : null,
      inner_ok: !!r.inner_ok,
      outer_mm: (typeof r.outer_mm === 'number') ? r.outer_mm : null,
      outer_ok: !!r.outer_ok
    }));
    res.setHeader('Cache-Control', 'no-store');
    res.json({
      ok: true,
      device_id: deviceId,
      from: from.toISOString(),
      to: end.toISOString(),
      stride,
      points
    });
  });

  app.get('/api/telemetry/range', requireAuthApi, async (req, res) => {
    const deviceId = pickDeviceId(req);
    const from = parseDateInput(req.query.from);
    const to = parseDateInput(req.query.to);
    const limit = clampInt(req.query.limit, 50, 20000, 2000);
    if (!from || !to) return res.status(400).json({ ok: false, error: 'bad_time_range' });
    if (from.getTime() >= to.getTime()) return res.status(400).json({ ok: false, error: 'bad_time_range' });

    const r = await pool.query(
      `SELECT ts, payload
       FROM telemetry
       WHERE device_id=$1 AND ts >= $2 AND ts <= $3
       ORDER BY ts ASC
       LIMIT $4`,
      [deviceId, from, to, limit]
    );
    const rows = (r.rows || []).map((x) => ({
      ts: x.ts ? new Date(x.ts).toISOString() : '',
      payload: x.payload || {}
    }));
    res.setHeader('Cache-Control', 'no-store');
    res.json({ ok: true, device_id: deviceId, rows });
  });

  // --- Devices ---
  app.get('/api/devices', requireAuthApi, async (req, res) => {
    const list = await listDevices(pool);
    res.setHeader('Cache-Control', 'no-store');
    res.json({ ok: true, devices: list });
  });

  // --- Admin APIs ---
  app.get('/api/admin/settings', requireAuthApi, requireAdmin, async (req, res) => {
    const v = await getSetting(pool, 'retention_days');
    res.json({ ok: true, retention_days: clampInt(v, 1, 3650, cfg.DATA_RETENTION_DAYS) });
  });

  app.post('/api/admin/settings', requireAuthApi, requireAdmin, async (req, res) => {
    const n = clampInt(req.body && req.body.retention_days, 1, 3650, cfg.DATA_RETENTION_DAYS);
    await setSetting(pool, 'retention_days', String(n));
    res.json({ ok: true, retention_days: n });
  });

  app.get('/api/admin/users', requireAuthApi, requireAdmin, async (req, res) => {
    const users = await listUsers(pool);
    res.json({ ok: true, users });
  });

  app.post('/api/admin/users', requireAuthApi, requireAdmin, async (req, res) => {
    const body = req.body || {};
    const username = String(body.username || '').trim();
    const password = String(body.password || '');
    const role = (String(body.role || 'user').trim().toLowerCase() === 'admin') ? 'admin' : 'user';
    if (!username || username.length > 64) return res.status(400).json({ ok: false, error: 'bad_username' });
    if (!password || password.length < 8) return res.status(400).json({ ok: false, error: 'bad_password' });
    const hash = await hashPassword(password);
    try {
      const u = await createUser(pool, username, hash, role);
      res.json({ ok: true, user: u });
    } catch (e) {
      res.status(400).json({ ok: false, error: 'user_exists' });
    }
  });

  app.post('/api/admin/users/:id/password', requireAuthApi, requireAdmin, async (req, res) => {
    const id = clampInt(req.params.id, 1, 1_000_000_000, 0);
    const password = String((req.body && req.body.password) || '');
    if (!id) return res.status(400).json({ ok: false, error: 'bad_id' });
    if (!password || password.length < 8) return res.status(400).json({ ok: false, error: 'bad_password' });
    const hash = await hashPassword(password);
    await setUserPassword(pool, id, hash);
    res.json({ ok: true });
  });

  app.post('/api/admin/users/:id/disable', requireAuthApi, requireAdmin, async (req, res) => {
    const id = clampInt(req.params.id, 1, 1_000_000_000, 0);
    const disabled = !!(req.body && req.body.disabled);
    if (!id) return res.status(400).json({ ok: false, error: 'bad_id' });
    await setUserDisabled(pool, id, disabled);
    res.json({ ok: true });
  });

  // --- Compatibility stubs for the built-in UI (device logs/config) ---
  app.get('/api/config', requireAuthApi, async (req, res) => {
    // Cloud panel does not push ctrl.json to devices yet; return a safe default shape.
    res.setHeader('Cache-Control', 'no-store');
    res.type('application/json').send(
      JSON.stringify(
        { mode: 'mixed', tz_offset_ms: 28800000, daily: [], cycle: [], leveldiff: [] },
        null,
        0
      )
    );
  });

  app.post('/api/config', requireAuthApi, async (req, res) => {
    // Accept and ignore for now (future: could store and sync to device).
    res.json({ ok: true });
  });

  app.get('/api/log', requireAuthApi, async (req, res) => {
    const name = String((req.query && req.query.name) || 'error');
    const text =
      'Cloud panel note:\\n' +
      '- Device logs are stored on the ESP32 (LittleFS) and are not exposed via MQTT by default.\\n' +
      '- Use /replay to query historical telemetry stored on the server.\\n\\n' +
      'Requested log: ' +
      name +
      '\\n';
    res.setHeader('Cache-Control', 'no-store');
    res.type('text/plain').send(text);
  });

  app.post('/api/log/clear', requireAuthApi, async (req, res) => {
    res.json({ ok: true });
  });

  app.get('/api/log/download', requireAuthApi, async (req, res) => {
    const name = String((req.query && req.query.name) || 'error');
    const text =
      'Cloud panel note: device logs are not available here.\\n' +
      'Use /replay for telemetry history.\\n' +
      'Requested log: ' +
      name +
      '\\n';
    res.setHeader('Cache-Control', 'no-store');
    res.setHeader('Content-Disposition', `attachment; filename=\"log_${name}.txt\"`);
    res.type('text/plain').send(text);
  });

  // --- Start ---
  app.listen(cfg.PORT, () => {
    // eslint-disable-next-line no-console
    console.log(`[http] listening on :${cfg.PORT} env=${cfg.NODE_ENV}`);
  });
}

main().catch((e) => {
  // eslint-disable-next-line no-console
  console.error('[fatal]', e);
  process.exit(1);
});

