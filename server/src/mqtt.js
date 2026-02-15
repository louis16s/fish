const mqtt = require('mqtt');

function inferDeviceIdFromTopic(topic) {
  const t = String(topic || '');
  const m = /^([^/]+)\/device\/telemetry(?:\/.*)?$/.exec(t);
  if (m && m[1]) return m[1];
  // Fallback: "fish1/device/telemetry" -> first segment still works even if suffix differs.
  const parts = t.split('/').filter(Boolean);
  if (parts.length >= 1) return parts[0];
  return '';
}

function createMqttClient(cfg, handlers) {
  const h = handlers || {};
  const state = {
    connected: false,
    lastError: '',
    lastConnectAt: 0
  };

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
    client.subscribe(cfg.MQTT_TELEMETRY_SUB, { qos: 0 }, (err) => {
      if (err) setErr(err);
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
    let payload = null;
    try {
      const s = payloadBuf ? payloadBuf.toString('utf8') : '';
      payload = s ? JSON.parse(s) : null;
    } catch (e) {
      if (typeof h.onBadMessage === 'function') h.onBadMessage(topicStr, e);
      return;
    }
    if (typeof h.onTelemetry === 'function') {
      h.onTelemetry({ deviceId, topic: topicStr, payload, receivedAt });
    }
  });

  function publishCommand(deviceId, cmd) {
    const did = String(deviceId || '').trim();
    if (!did) throw new Error('missing deviceId');
    const topic = did + '/device/command';
    const body = JSON.stringify({ cmd: String(cmd || '') });
    return new Promise((resolve, reject) => {
      client.publish(topic, body, { qos: 0, retain: false }, (err) => {
        if (err) return reject(err);
        resolve({ topic });
      });
    });
  }

  return { client, state, publishCommand };
}

module.exports = { createMqttClient };

