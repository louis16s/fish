import 'dotenv/config';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import express from 'express';
import session from 'express-session';
import rateLimit from 'express-rate-limit';
import cookieParser from 'cookie-parser';
import mqtt from 'mqtt';
import { WebSocketServer } from 'ws';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

function mustEnv(name, fallback = '') {
  const v = (process.env[name] ?? fallback).toString();
  if (!v) throw new Error(`missing env: ${name}`);
  return v;
}

const PORT = parseInt(process.env.PORT ?? '3000', 10);
const SESSION_SECRET = mustEnv('SESSION_SECRET', 'CHANGE_ME');
const ADMIN_USER = (process.env.ADMIN_USER ?? 'admin').toString();
const ADMIN_PASS = mustEnv('ADMIN_PASS', 'CHANGE_ME');
const SESSION_COOKIE_SECURE = (process.env.SESSION_COOKIE_SECURE ?? '').toString().toLowerCase() === 'true';

const MQTT_URL = mustEnv('MQTT_URL', 'mqtt://127.0.0.1:1883');
const MQTT_USERNAME = (process.env.MQTT_USERNAME ?? '').toString();
const MQTT_PASSWORD = (process.env.MQTT_PASSWORD ?? '').toString();
const MQTT_TELEMETRY_TOPIC = mustEnv('MQTT_TELEMETRY_TOPIC', 'fish1/device/telemetry');
const MQTT_COMMAND_TOPIC = mustEnv('MQTT_COMMAND_TOPIC', 'fish1/device/command');

const app = express();
app.set('trust proxy', 1);

app.use(rateLimit({ windowMs: 60_000, limit: 120 }));
app.use(cookieParser());
app.use(express.json({ limit: '256kb' }));
app.use(express.urlencoded({ extended: false }));
app.use(
  session({
    secret: SESSION_SECRET,
    resave: false,
    saveUninitialized: false,
    cookie: {
      httpOnly: true,
      sameSite: 'lax',
      // Keep HTTP workable by default; enable after HTTPS is configured.
      secure: SESSION_COOKIE_SECURE
    }
  })
);

function authed(req) {
  return Boolean(req.session && req.session.user === 'admin');
}

function requireAuth(req, res, next) {
  if (authed(req)) return next();
  return res.redirect('/login');
}

let lastTelemetry = null;
let lastTelemetryAt = 0;

const mqttClient = mqtt.connect(MQTT_URL, {
  username: MQTT_USERNAME || undefined,
  password: MQTT_PASSWORD || undefined,
  reconnectPeriod: 2000,
  connectTimeout: 10_000,
  clean: true
});

mqttClient.on('connect', () => {
  mqttClient.subscribe(MQTT_TELEMETRY_TOPIC, { qos: 0 }, (err) => {
    if (err) console.error('[mqtt] subscribe error', err);
  });
  console.log('[mqtt] connected', MQTT_URL, 'telemetry=', MQTT_TELEMETRY_TOPIC, 'cmd=', MQTT_COMMAND_TOPIC);
});

mqttClient.on('message', (topic, payload) => {
  if (topic !== MQTT_TELEMETRY_TOPIC) return;
  try {
    const s = payload.toString('utf8');
    const obj = JSON.parse(s);
    lastTelemetry = obj;
    lastTelemetryAt = Date.now();
    broadcast({ type: 'telemetry', at: lastTelemetryAt, data: lastTelemetry });
  } catch (e) {
    console.error('[mqtt] telemetry parse error', e);
  }
});

mqttClient.on('error', (err) => {
  console.error('[mqtt] error', err);
});

app.get('/healthz', (_req, res) => {
  res.json({
    ok: true,
    mqtt_connected: mqttClient.connected,
    telemetry_topic: MQTT_TELEMETRY_TOPIC,
    command_topic: MQTT_COMMAND_TOPIC,
    last_telemetry_age_s: lastTelemetryAt ? Math.floor((Date.now() - lastTelemetryAt) / 1000) : null
  });
});

app.get('/login', (req, res) => {
  if (authed(req)) return res.redirect('/');
  res.sendFile(path.join(__dirname, 'public', 'login.html'));
});

const loginLimiter = rateLimit({ windowMs: 60_000, limit: 10 });

app.post('/login', loginLimiter, (req, res) => {
  const u = (req.body?.username ?? '').toString();
  const p = (req.body?.password ?? '').toString();
  if (u === ADMIN_USER && p === ADMIN_PASS) {
    req.session.user = 'admin';
    return res.redirect('/');
  }
  return res.redirect('/login?e=1');
});

app.post('/logout', (req, res) => {
  req.session.destroy(() => res.redirect('/login'));
});

app.get('/', requireAuth, (_req, res) => {
  res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

app.get('/api/state', requireAuth, (_req, res) => {
  res.json({
    mqtt_connected: mqttClient.connected,
    last_telemetry_at: lastTelemetryAt || null,
    telemetry: lastTelemetry
  });
});

function publishCmd(cmd) {
  const payload = JSON.stringify({ cmd });
  mqttClient.publish(MQTT_COMMAND_TOPIC, payload, { qos: 0, retain: false });
}

app.post('/api/cmd', requireAuth, (req, res) => {
  const cmd = (req.body?.cmd ?? '').toString();
  const allowed = new Set([
    'gate_open',
    'gate_close',
    'gate_stop',
    'auto_on',
    'auto_off',
    'auto_latch_off',
    'manual_end'
  ]);
  if (!allowed.has(cmd)) return res.status(400).json({ ok: false, error: 'bad_cmd' });
  if (!mqttClient.connected) return res.status(503).json({ ok: false, error: 'mqtt_disconnected' });

  publishCmd(cmd);
  return res.json({ ok: true });
});

app.use('/static', express.static(path.join(__dirname, 'public', 'static'), { maxAge: '1h' }));

const server = http.createServer(app);
const wss = new WebSocketServer({ server, path: '/ws' });

function broadcast(msg) {
  const data = JSON.stringify(msg);
  for (const ws of wss.clients) {
    if (ws.readyState === 1) ws.send(data);
  }
}

wss.on('connection', (ws, req) => {
  // Minimal protection: only allow WS from authenticated cookie sessions.
  // For simplicity we just require the session cookie to exist; real enforcement happens via nginx same-origin.
  const cookie = (req.headers.cookie ?? '').toString();
  if (!cookie.includes('connect.sid=')) {
    ws.close();
    return;
  }
  ws.send(JSON.stringify({ type: 'hello', mqtt_connected: mqttClient.connected }));
  if (lastTelemetry) {
    ws.send(JSON.stringify({ type: 'telemetry', at: lastTelemetryAt, data: lastTelemetry }));
  }
});

server.listen(PORT, '0.0.0.0', () => {
  console.log(`[web] listening on :${PORT}`);
});
