const pg = require('pg');

function makePool(databaseUrl) {
  return new pg.Pool({
    connectionString: databaseUrl,
    max: 10,
    idleTimeoutMillis: 30_000
  });
}

async function initDb(pool) {
  // Keep schema minimal and "idempotent" (CREATE IF NOT EXISTS).
  await pool.query(`
    CREATE TABLE IF NOT EXISTS telemetry (
      id BIGSERIAL PRIMARY KEY,
      device_id TEXT NOT NULL,
      ts TIMESTAMPTZ NOT NULL DEFAULT now(),
      payload JSONB NOT NULL,
      mqtt_topic TEXT NOT NULL DEFAULT '',
      created_at TIMESTAMPTZ NOT NULL DEFAULT now()
    );
  `);
  await pool.query(`CREATE INDEX IF NOT EXISTS telemetry_device_ts_idx ON telemetry(device_id, ts DESC);`);

  await pool.query(`
    CREATE TABLE IF NOT EXISTS devices (
      device_id TEXT PRIMARY KEY,
      display_name TEXT NOT NULL DEFAULT '',
      created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
      last_seen_at TIMESTAMPTZ
    );
  `);

  await pool.query(`
    CREATE TABLE IF NOT EXISTS users (
      id SERIAL PRIMARY KEY,
      username TEXT NOT NULL UNIQUE,
      password_hash TEXT NOT NULL,
      role TEXT NOT NULL DEFAULT 'user',
      disabled BOOLEAN NOT NULL DEFAULT false,
      created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
      updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
    );
  `);
  await pool.query(`CREATE INDEX IF NOT EXISTS users_username_idx ON users(username);`);

  await pool.query(`
    CREATE TABLE IF NOT EXISTS settings (
      key TEXT PRIMARY KEY,
      value TEXT NOT NULL,
      updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
    );
  `);
}

async function getSetting(pool, key) {
  const r = await pool.query('SELECT value FROM settings WHERE key=$1', [key]);
  return r.rows && r.rows[0] ? r.rows[0].value : null;
}

async function setSetting(pool, key, value) {
  await pool.query(
    `INSERT INTO settings(key,value,updated_at)
     VALUES($1,$2,now())
     ON CONFLICT (key) DO UPDATE SET value=EXCLUDED.value, updated_at=now()`,
    [key, String(value)]
  );
}

async function ensureAdminUser(pool, username, passwordHash) {
  const r = await pool.query(`SELECT COUNT(*)::int AS n FROM users WHERE role='admin' AND disabled=false`);
  const n = r.rows && r.rows[0] ? (r.rows[0].n | 0) : 0;
  if (n > 0) return { created: false };

  await pool.query(
    `INSERT INTO users(username,password_hash,role,disabled,created_at,updated_at)
     VALUES($1,$2,'admin',false,now(),now())
     ON CONFLICT (username) DO UPDATE SET
       password_hash=EXCLUDED.password_hash,
       role='admin',
       disabled=false,
       updated_at=now()`,
    [username, passwordHash]
  );
  return { created: true };
}

async function findUserByUsername(pool, username) {
  const r = await pool.query(
    `SELECT id, username, password_hash, role, disabled
     FROM users
     WHERE username=$1
     LIMIT 1`,
    [username]
  );
  return r.rows && r.rows[0] ? r.rows[0] : null;
}

async function listUsers(pool) {
  const r = await pool.query(
    `SELECT id, username, role, disabled, created_at, updated_at
     FROM users
     ORDER BY id ASC`
  );
  return r.rows || [];
}

async function createUser(pool, username, passwordHash, role) {
  const r = await pool.query(
    `INSERT INTO users(username,password_hash,role,disabled,created_at,updated_at)
     VALUES($1,$2,$3,false,now(),now())
     RETURNING id, username, role, disabled, created_at, updated_at`,
    [username, passwordHash, role]
  );
  return r.rows[0];
}

async function setUserPassword(pool, id, passwordHash) {
  await pool.query(`UPDATE users SET password_hash=$2, updated_at=now() WHERE id=$1`, [id, passwordHash]);
}

async function setUserDisabled(pool, id, disabled) {
  await pool.query(`UPDATE users SET disabled=$2, updated_at=now() WHERE id=$1`, [id, !!disabled]);
}

async function upsertDeviceSeen(pool, deviceId, seenAt) {
  await pool.query(
    `INSERT INTO devices(device_id,last_seen_at)
     VALUES($1,$2)
     ON CONFLICT (device_id) DO UPDATE SET last_seen_at=EXCLUDED.last_seen_at`,
    [deviceId, seenAt]
  );
}

async function listDevices(pool) {
  const r = await pool.query(
    `SELECT device_id, display_name, created_at, last_seen_at
     FROM devices
     ORDER BY COALESCE(last_seen_at, created_at) DESC`
  );
  return r.rows || [];
}

async function insertTelemetry(pool, deviceId, receivedAt, payload, mqttTopic) {
  await pool.query(
    `INSERT INTO telemetry(device_id, ts, payload, mqtt_topic)
     VALUES($1,$2,$3::jsonb,$4)`,
    [deviceId, new Date(receivedAt), JSON.stringify(payload || {}), mqttTopic || '']
  );
}

async function getLatestTelemetry(pool, deviceId) {
  const r = await pool.query(
    `SELECT ts, payload
     FROM telemetry
     WHERE device_id=$1
     ORDER BY ts DESC
     LIMIT 1`,
    [deviceId]
  );
  return r.rows && r.rows[0] ? r.rows[0] : null;
}

async function cleanupTelemetry(pool, retentionDays) {
  const days = Math.max(1, (retentionDays | 0) || 30);
  await pool.query(`DELETE FROM telemetry WHERE ts < (now() - ($1::int * interval '1 day'))`, [days]);
}

async function countTelemetry(pool, deviceId, fromTs, toTs) {
  const r = await pool.query(
    `SELECT COUNT(*)::int AS n
     FROM telemetry
     WHERE device_id=$1 AND ts >= $2 AND ts <= $3`,
    [deviceId, fromTs, toTs]
  );
  return r.rows && r.rows[0] ? (r.rows[0].n | 0) : 0;
}

async function queryHistoryDownsampled(pool, deviceId, fromTs, toTs, stride) {
  const s = Math.max(1, (stride | 0) || 1);
  const r = await pool.query(
    `
    SELECT
      extract(epoch from ts)::bigint AS ts_s,
      NULLIF(payload->'sensor1'->>'mm','')::int AS inner_mm,
      COALESCE((payload->'sensor1'->>'valid')::boolean,false) AS inner_ok,
      NULLIF(payload->'sensor2'->>'mm','')::int AS outer_mm,
      COALESCE((payload->'sensor2'->>'valid')::boolean,false) AS outer_ok
    FROM (
      SELECT ts, payload,
             row_number() OVER (ORDER BY ts ASC) AS rn
      FROM telemetry
      WHERE device_id=$1 AND ts >= $2 AND ts <= $3
    ) t
    WHERE ((rn - 1) % $4) = 0
    ORDER BY ts ASC
    `,
    [deviceId, fromTs, toTs, s]
  );
  return r.rows || [];
}

module.exports = {
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
};

