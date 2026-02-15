const mqtt = require('mqtt');

function inferDeviceIdFromTopic(topic) {
  const t = String(topic || '');
  const m = /^([^/]+)\/device\/(?:telemetry|reply|log)(?:\/.*)?$/.exec(t);
  if (m && m[1]) return m[1];
  // Fallback: "fish1/device/telemetry" -> first segment still works even if suffix differs.
  const parts = t.split('/').filter(Boolean);
  if (parts.length >= 1) return parts[0];
  return '';
}

function isReplyTopic(topic) {
  const t = String(topic || '');
  return /\/device\/reply(?:\/.*)?$/.test(t);
}

function isTelemetryTopic(topic) {
  const t = String(topic || '');
  return /\/device\/telemetry(?:\/.*)?$/.test(t);
}

function isLogTopic(topic) {
  const t = String(topic || '');
  return /\/device\/log(?:\/.*)?$/.test(t);
}

function inferLogNameFromTopic(topic) {
  const parts = String(topic || '').split('/').filter(Boolean);
  // <deviceId>/device/log/<name>
  const i = parts.findIndex((p) => p === 'log');
  if (i >= 0 && parts[i + 1]) return String(parts[i + 1]).toLowerCase();
  return 'error';
}

function makeReqId() {
  // Simple, collision-resistant enough for in-process correlation.
  return Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 10);
}

function createMqttClient(cfg, handlers) {
  const h = handlers || {};
  const state = {
    connected: false,
    lastError: '',
    lastConnectAt: 0
  };

  const pending = new Map(); // reqId -> { deviceId, resolve, reject, timeout }

  const url = cfg.MQTT_URL;
  const opts = {
    username: cfg.MQTT_USERNAME || undefined,
    password: cfg.MQTT_PASSWORD || undefined,
    reconnectPeriod: 2000,
    connectTimeout: 10_000,
    // Keep it conservative for embedded brokers/links.
    keepalive: 30,
    clean: true
  };

  const client = mqtt.connect(url, opts);

  const setErr = (e) => {
    state.lastError = (e && e.message) ? String(e.message) : String(e || '');
  };

  client.on('connect', () => {
    state.connected = true;
    state.lastError = '';
    state.lastConnectAt = Date.now();
    if (typeof h.onConnect === 'function') h.onConnect();
    const subs = [cfg.MQTT_TELEMETRY_SUB, cfg.MQTT_REPLY_SUB, cfg.MQTT_LOG_SUB].filter(Boolean);
    const uniq = Array.from(new Set(subs));
    uniq.forEach((topic) => {
      client.subscribe(topic, { qos: 0 }, (err) => {
        if (err) setErr(err);
      });
    });
  });

  client.on('reconnect', () => {
    state.connected = false;
  });

  client.on('close', () => {
    state.connected = false;
  });

  client.on('offline', () => {
    state.connected = false;
  });

  client.on('error', (err) => {
    setErr(err);
  });

  client.on('message', (topic, payloadBuf) => {
    const receivedAt = Date.now();
    const topicStr = String(topic || '');
    const deviceId = inferDeviceIdFromTopic(topicStr) || cfg.DEFAULT_DEVICE_ID;

    // Logs are pushed as plain text (not JSON). Parse by topic type first.
    if (isLogTopic(topicStr)) {
      const text = payloadBuf ? payloadBuf.toString('utf8') : '';
      const name = inferLogNameFromTopic(topicStr);
      if (typeof h.onLog === 'function') {
        h.onLog({ deviceId, name, topic: topicStr, text, receivedAt });
      }
      return;
    }

    let payload = null;
    try {
      const s = payloadBuf ? payloadBuf.toString('utf8') : '';
      payload = s ? JSON.parse(s) : null;
    } catch (e) {
      if (typeof h.onBadMessage === 'function') h.onBadMessage(topicStr, e);
      return;
    }

    if (isReplyTopic(topicStr)) {
      const reqId = payload && payload.req_id ? String(payload.req_id) : '';
      if (reqId && pending.has(reqId)) {
        const p = pending.get(reqId);
        pending.delete(reqId);
        try {
          if (p && p.timeout) clearTimeout(p.timeout);
        } catch (e) {}
        // Best-effort: ensure device matches to avoid cross-device collisions.
        if (p && p.deviceId && deviceId && p.deviceId !== deviceId) {
          try {
            p.reject(new Error('bad_device'));
          } catch (e) {}
          return;
        }
        try {
          p.resolve(payload || {});
        } catch (e) {}
      }
      if (typeof h.onReply === 'function') {
        h.onReply({ deviceId, topic: topicStr, payload, receivedAt });
      }
      return;
    }

    if (!isTelemetryTopic(topicStr)) {
      // Ignore unknown topics to avoid polluting telemetry storage.
      return;
    }

    if (typeof h.onTelemetry === 'function') {
      h.onTelemetry({ deviceId, topic: topicStr, payload, receivedAt });
    }
  });

  function publishMessage(deviceId, msgObj) {
    const did = String(deviceId || '').trim();
    if (!did) throw new Error('missing deviceId');
    const topic = did + '/device/command';
    if (!state.connected) throw new Error('mqtt_not_connected');
    const body = JSON.stringify(msgObj || {});
    return new Promise((resolve, reject) => {
      client.publish(topic, body, { qos: 0, retain: false }, (err) => {
        if (err) return reject(err);
        resolve({ topic });
      });
    });
  }

  function publishCommand(deviceId, cmd) {
    return publishMessage(deviceId, { cmd: String(cmd || '') });
  }

  function rpc(deviceId, msgObj, options) {
    const did = String(deviceId || '').trim();
    if (!did) return Promise.reject(new Error('missing deviceId'));
    if (!state.connected) return Promise.reject(new Error('mqtt_not_connected'));

    const msg = msgObj && typeof msgObj === 'object' ? { ...msgObj } : {};
    const cmd = String(msg.cmd || '').trim();
    if (!cmd) return Promise.reject(new Error('missing_cmd'));

    const timeoutMs = (options && Number.isFinite(options.timeoutMs)) ? Math.max(500, options.timeoutMs | 0) : 6000;
    const reqId = makeReqId();
    msg.req_id = reqId;

    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        pending.delete(reqId);
        reject(new Error('timeout'));
      }, timeoutMs);
      pending.set(reqId, { deviceId: did, resolve, reject, timeout });

      publishMessage(did, msg).catch((e) => {
        pending.delete(reqId);
        try {
          clearTimeout(timeout);
        } catch (e2) {}
        reject(e);
      });
    }).then((reply) => {
      const ok = !!(reply && reply.ok);
      if (!ok) {
        const err = reply && reply.error ? String(reply.error) : 'device_error';
        throw new Error(err);
      }
      return reply;
    });
  }

  return { client, state, publishCommand, publishMessage, rpc };
}

module.exports = { createMqttClient };
