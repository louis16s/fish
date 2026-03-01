const api = require('../../utils/api');
const fmt = require('../../utils/format');
const STATE_POLL_MS = 2500;
const LOG_POLL_MS = 10000;

function pickDeviceId() {
  const app = getApp();
  return app.globalData.currentDeviceId || '';
}

function withDev(path) {
  const did = pickDeviceId();
  const q = api.buildQuery({ device_id: did });
  return `${path}${q}`;
}

function countEnabled(arr) {
  if (!Array.isArray(arr)) return 0;
  return arr.filter((x) => x && x.en).length;
}

function clamp(n, a, b) {
  return Math.min(b, Math.max(a, n));
}

function gateTargetFromTelemetry(t, gateStateNum) {
  const vals = [
    t && t.gate_open_pct,
    t && t.gate_open_percent,
    t && t.gate_open_ratio
  ];
  for (let i = 0; i < vals.length; i += 1) {
    const v = Number(vals[i]);
    if (!Number.isFinite(v)) continue;
    if (v >= 0 && v <= 1) return clamp(v, 0, 1);
    if (v >= 0 && v <= 100) return clamp(v / 100, 0, 1);
  }
  if (Number(gateStateNum) === 1) return 1;
  if (Number(gateStateNum) === 2) return 0;
  return null;
}

const CMD_LABELS = {
  gate_open: '开闸',
  gate_close: '关闸',
  gate_stop: '停止',
  auto_on: '开启自动',
  auto_off: '手动接管',
  auto_latch_off: '关闭自动',
  manual_end: '恢复自动'
};

Page({
  data: {
    userText: '--',
    deviceOptions: [],
    deviceIndex: 0,
    currentDeviceLabel: '--',
    mqttText: '--',
    mqttTagClass: '',
    serverMqttText: '--',
    serverMqttTagClass: '',
    deviceOnlineText: '--',
    deviceOnlineTagClass: '',
    lastTelemetryAt: '--',
    fwVersion: '--',
    innerText: '--',
    outerText: '--',
    deltaText: '--',
    gateText: '--',
    autoText: '--',
    alarmText: '--',
    cmdLoading: false,
    cmdMsg: '',
    cfgSummary: { mode: '--', tz: '--', daily: '--', cycle: '--', leveldiff: '--', msg: '' },
    logTab: 'error',
    logText: '加载中…',
    logMsg: '',
    schematicMsg: '',
    gateTagClass: '',
    deltaTagClass: '',
    innerMm: null,
    outerMm: null,
    deltaMm: null,
    gateStateNum: 0,
    gateProgress: 0,
    pageStatusClass: 'is-warn',
    statusHintText: '等待设备状态...',
    canOperate: false,
    deviceOnline: false
  },

  onReady() {
    this.initSchematicCanvas();
    this.startSchematicAnim();
  },

  onShow() {
    this.bootstrap();
    this.startPolling();
    this.startLogPolling();
    this.startSchematicAnim();
  },

  onHide() {
    this.stopPolling();
    this.stopLogPolling();
    this.stopSchematicAnim();
  },

  onUnload() {
    this.stopPolling();
    this.stopLogPolling();
    this.stopSchematicAnim();
  },

  async bootstrap() {
    try {
      const me = await api.requestJSON('/api/auth/me');
      const app = getApp();
      app.globalData.user = me.user || null;
      this.setData({ userText: `用户 ${me.user.username} | ${me.user.role}` });
    } catch (e) {
      wx.reLaunch({ url: '/pages/login/index' });
      return;
    }

    await this.loadDevices();
    await this.refreshAll();
  },

  startPolling() {
    this.stopPolling();
    this._tm = setInterval(() => {
      this.loadState();
    }, STATE_POLL_MS);
  },

  stopPolling() {
    if (this._tm) {
      clearInterval(this._tm);
      this._tm = 0;
    }
  },

  startLogPolling() {
    this.stopLogPolling();
    this._logTm = setInterval(() => {
      this.loadLog();
    }, LOG_POLL_MS);
  },

  stopLogPolling() {
    if (this._logTm) {
      clearInterval(this._logTm);
      this._logTm = 0;
    }
  },

  async loadDevices() {
    try {
      const j = await api.requestJSON('/api/devices');
      const devices = Array.isArray(j.devices) ? j.devices : [];
      const opts = devices.map((d) => ({ label: d.device_id || '', value: d.device_id || '' }));
      const app = getApp();
      app.globalData.devices = devices;

      let did = app.globalData.currentDeviceId;
      if (!did && opts.length) did = opts[0].value;
      const idx = Math.max(0, opts.findIndex((x) => x.value === did));
      const current = opts[idx] || { label: '(暂无设备)', value: '' };
      app.globalData.currentDeviceId = current.value || '';
      wx.setStorageSync('current_device_id', app.globalData.currentDeviceId || '');
      this.setData({ deviceOptions: opts, deviceIndex: idx, currentDeviceLabel: current.label });
    } catch (e) {
      this.setData({ deviceOptions: [], deviceIndex: 0, currentDeviceLabel: '(加载失败)' });
    }
  },

  async loadState() {
    try {
      let t = {};
      let lastAt = 0;
      let mqttConnected = false;

      const j = await api.requestJSON(withDev('/api/state'));
      t = j.telemetry || {};
      lastAt = j.last_telemetry_at || 0;
      mqttConnected = !!j.mqtt_connected;

      const s1 = t.sensor1 || {};
      const s2 = t.sensor2 || {};
      const inner = s1.valid ? Number(s1.mm) : null;
      const outer = s2.valid ? Number(s2.mm) : null;
      const delta = (inner != null && outer != null) ? (inner - outer) : null;
      const fw = (t.fw && t.fw.current) ? t.fw.current : (t.fw_version || t.fw || '--');
      const auto = t.auto_latched ? '锁定关' : (t.auto_gate ? '开' : '关');
      const gateStateNum = Number(t.gate_state || 0);
      const gateTarget = gateTargetFromTelemetry(t, gateStateNum);
      if (gateTarget != null) {
        this._gateRatioTarget = gateTarget;
        if (!Number.isFinite(this._gateRatioCurrent)) this._gateRatioCurrent = gateTarget;
      }
      const deltaTagClass = delta == null ? '' : (Math.abs(delta) > 80 ? 'tag-warn' : 'tag-good');
      const now = Date.now();
      const ageMs = lastAt > 0 ? Math.max(0, now - Number(lastAt || 0)) : 9e9;
      const deviceOnline = ageMs <= 15000;
      const gateProgress = Math.round(100 * (Number.isFinite(this._gateRatioTarget) ? this._gateRatioTarget : (gateStateNum === 1 ? 1 : (gateStateNum === 2 ? 0 : 0.5))));
      const canOperate = !!(mqttConnected && deviceOnline);
      const gateText = deviceOnline ? fmt.gateStateText(t.gate_state) : '离线';
      const gateTagClass = deviceOnline
        ? (gateStateNum === 1 ? 'tag-good' : (gateStateNum === 2 ? 'tag-bad' : 'tag-warn'))
        : 'tag-bad';
      const statusHintText = !mqttConnected
        ? '服务器 MQTT 未连接，暂不可控'
        : (deviceOnline ? '设备在线，可执行控制命令' : '设备离线，控制命令可能超时');

      this.setData({
        mqttText: mqttConnected ? '已连接' : '未连接',
        mqttTagClass: mqttConnected ? 'tag-good' : 'tag-bad',
        serverMqttText: mqttConnected ? '服务器 已连接' : '服务器 未连接',
        serverMqttTagClass: mqttConnected ? 'tag-good' : 'tag-bad',
        deviceOnlineText: deviceOnline ? '设备状态 在线' : '设备状态 离线',
        deviceOnlineTagClass: deviceOnline ? 'tag-good' : 'tag-bad',
        deviceOnline,
        lastTelemetryAt: fmt.fmtDateTime(lastAt || 0),
        fwVersion: fw || '--',
        innerText: inner == null ? '--' : `${inner} mm`,
        outerText: outer == null ? '--' : `${outer} mm`,
        deltaText: delta == null ? '--' : `${delta} mm`,
        gateText,
        autoText: auto,
        alarmText: fmt.alarmText(t.alarm),
        gateTagClass,
        deltaTagClass,
        innerMm: inner,
        outerMm: outer,
        deltaMm: delta,
        gateStateNum,
        gateProgress,
        canOperate,
        statusHintText,
        pageStatusClass: canOperate ? 'is-good' : 'is-warn'
      });
      this.drawSchematic();
    } catch (e) {
      this.setData({
        mqttText: '异常',
        mqttTagClass: 'tag-bad',
        serverMqttText: '服务器 状态未知',
        serverMqttTagClass: 'tag-bad',
        deviceOnlineText: '设备状态 未知',
        deviceOnlineTagClass: 'tag-bad',
        deviceOnline: false,
        gateProgress: 0,
        canOperate: false,
        statusHintText: '状态获取失败，请检查网络后重试',
        pageStatusClass: 'is-warn'
      });
    }
  },

  async sendCmd(cmd) {
    this.setData({ cmdLoading: true, cmdMsg: '' });
    try {
      await api.requestJSON(withDev('/api/cmd'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        data: { cmd }
      });
      this.setData({ cmdMsg: `已发送：${CMD_LABELS[cmd] || cmd}` });
      await this.loadState();
    } catch (e) {
      this.setData({ cmdMsg: `发送失败：${e.message}` });
    } finally {
      this.setData({ cmdLoading: false });
    }
  },

  cmdGateOpen() { this.sendCmd('gate_open'); },
  cmdGateClose() { this.sendCmd('gate_close'); },
  cmdGateStop() { this.sendCmd('gate_stop'); },
  cmdAutoOn() { this.sendCmd('auto_on'); },
  cmdAutoOff() { this.sendCmd('auto_off'); },
  cmdAutoLatchOff() { this.sendCmd('auto_latch_off'); },
  cmdManualEnd() { this.sendCmd('manual_end'); },

  async loadConfigSummary() {
    const empty = { mode: '--', tz: '--', daily: '--', cycle: '--', leveldiff: '--', msg: '' };
    try {
      let raw = '';
      const cacheUrl = `${withDev('/api/config')}${withDev('/api/config').includes('?') ? '&' : '?'}source=cache`;
      try {
        raw = await api.requestText(cacheUrl);
      } catch (e) {
        raw = await api.requestText(withDev('/api/config'));
      }
      const cfg = fmt.safeParseJSON(raw, {});
      const tzMs = Number(cfg.tz_offset_ms || 0);
      const tzH = Number.isFinite(tzMs) ? (tzMs / 3600000) : 8;
      this.setData({
        cfgSummary: {
          mode: cfg.mode || 'mixed',
          tz: `UTC${tzH >= 0 ? '+' : ''}${tzH}`,
          daily: `${countEnabled(cfg.daily)} / ${Array.isArray(cfg.daily) ? cfg.daily.length : 0}`,
          cycle: `${countEnabled(cfg.cycle)} / ${Array.isArray(cfg.cycle) ? cfg.cycle.length : 0}`,
          leveldiff: `${countEnabled(cfg.leveldiff)} / ${Array.isArray(cfg.leveldiff) ? cfg.leveldiff.length : 0}`,
          msg: ''
        }
      });
    } catch (e) {
      empty.msg = `读取失败：${e.message}`;
      this.setData({ cfgSummary: empty });
    }
  },

  async loadLog() {
    if (this._logInflight) return;
    this._logInflight = true;
    const reqId = (this._logReqId || 0) + 1;
    this._logReqId = reqId;
    const tab = this.data.logTab;
    const urlBase = `${withDev('/api/log')}${withDev('/api/log').includes('?') ? '&' : '?'}name=${encodeURIComponent(tab)}&tail=16384`;
    try {
      let text = '';
      let msg = '';
      try {
        text = await api.requestText(`${urlBase}&source=cache`);
      } catch (e) {
        text = await api.requestText(urlBase);
        msg = '缓存未命中，已回退设备 RPC';
      }
      if (reqId === this._logReqId) {
        this.setData({ logText: text || '(空)', logMsg: msg });
      }
    } catch (e) {
      if (reqId === this._logReqId) {
        this.setData({ logMsg: `读取失败：${e.message}` });
      }
    } finally {
      if (reqId === this._logReqId) this._logInflight = false;
    }
  },

  async clearLog() {
    try {
      await api.requestJSON(withDev('/api/log/clear'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        data: { name: this.data.logTab }
      });
      this.setData({ logMsg: '已清空' });
      await this.loadLog();
    } catch (e) {
      this.setData({ logMsg: `清空失败：${e.message}` });
    }
  },

  async refreshAll() {
    await this.loadState();
    await this.loadConfigSummary();
    await this.loadLog();
  },

  refreshLog() {
    this.loadLog();
  },

  setLogTab(e) {
    const tab = (e.currentTarget.dataset.tab || 'error');
    this.setData({ logTab: tab });
    this.loadLog();
  },

  onDeviceChange(e) {
    const idx = Number(e.detail.value) || 0;
    const item = this.data.deviceOptions[idx];
    const did = item ? item.value : '';
    const app = getApp();
    app.globalData.currentDeviceId = did;
    wx.setStorageSync('current_device_id', did || '');
    this.setData({ deviceIndex: idx, currentDeviceLabel: item ? item.label : '--' });
    this.refreshAll();
  },

  initSchematicCanvas() {
    if (this._schemInitDone) return;
    this._schemInitDone = true;
    const query = wx.createSelectorQuery().in(this);
    query.select('#schematicCanvas').fields({ node: true, size: true }).exec((res) => {
      if (!res || !res[0] || !res[0].node) {
        this.setData({ schematicMsg: '示意图初始化失败：canvas 不可用。' });
        return;
      }
      const { node, width, height } = res[0];
      const dpr = (wx.getSystemInfoSync().pixelRatio || 1);
      node.width = width * dpr;
      node.height = height * dpr;
      const ctx = node.getContext('2d');
      ctx.scale(dpr, dpr);
      this._schemCanvas = node;
      this._schemCtx = ctx;
      this._schemW = width;
      this._schemH = height;
      this.drawSchematic();
    });
  },

  startSchematicAnim() {
    if (this._schemAnimTimer) return;
    this._lastAnimTs = 0;
    this._flowPhase = Number.isFinite(this._flowPhase) ? this._flowPhase : 0;
    if (!Number.isFinite(this._gateRatioCurrent)) this._gateRatioCurrent = 0;
    if (!Number.isFinite(this._gateRatioTarget)) this._gateRatioTarget = 0;
    this._schemAnimTimer = setInterval(() => {
      if (!this._schemCtx || !this._schemW || !this._schemH) return;
      const now = Date.now();
      const dt = this._lastAnimTs ? Math.max(0.016, (now - this._lastAnimTs) / 1000) : 0.016;
      this._lastAnimTs = now;

      const cur = Number.isFinite(this._gateRatioCurrent) ? this._gateRatioCurrent : 0;
      const target = Number.isFinite(this._gateRatioTarget) ? this._gateRatioTarget : cur;
      // Exponential follow gives smoother start/stop than fixed-step movement.
      const follow = 1 - Math.pow(0.02, dt / 1.5);
      this._gateRatioCurrent = clamp(cur + (target - cur) * follow, 0, 1);

      this._flowPhase = (this._flowPhase + dt * 1.2) % 1;
      this.drawSchematic();
    }, 20);
  },

  stopSchematicAnim() {
    if (this._schemAnimTimer) {
      clearInterval(this._schemAnimTimer);
      this._schemAnimTimer = 0;
    }
  },

  drawSchematic() {
    const ctx = this._schemCtx;
    const W = this._schemW;
    const H = this._schemH;
    if (!ctx || !W || !H) return;

    const inner = this.data.innerMm;
    const outer = this.data.outerMm;
    const delta = this.data.deltaMm;
    const gateState = Number(this.data.gateStateNum || 0);
    const deviceOnline = !!this.data.deviceOnline;
    const gateRatio = clamp(Number.isFinite(this._gateRatioCurrent) ? this._gateRatioCurrent : (gateState === 1 ? 1 : 0), 0, 1);
    const wavePhase = (Number.isFinite(this._flowPhase) ? this._flowPhase : 0) * Math.PI * 2;

    ctx.clearRect(0, 0, W, H);

    const bg = ctx.createLinearGradient(0, 0, 0, H);
    bg.addColorStop(0, 'rgba(8,16,36,0.95)');
    bg.addColorStop(1, 'rgba(6,12,26,0.98)');
    ctx.fillStyle = bg;
    ctx.fillRect(0, 0, W, H);

    const margin = 20;
    const pondTop = 44;
    const pondBottom = H - 34;
    const pondH = pondBottom - pondTop;
    const leftX = margin;
    const leftW = (W - margin * 2 - 80) / 2;
    const gateX = leftX + leftW;
    const gateW = 80;
    const rightX = gateX + gateW;
    const rightW = leftW;

    const frameStroke = 'rgba(226,232,240,0.9)';
    ctx.strokeStyle = frameStroke;
    ctx.lineWidth = 4;
    ctx.strokeRect(leftX, pondTop, leftW, pondH);
    ctx.strokeRect(rightX, pondTop, rightW, pondH);
    ctx.strokeRect(gateX + 14, pondTop, gateW - 28, pondH);

    // Vertical ruler: 0m~5m marks (every 1m) to match panel water-level scale.
    ctx.strokeStyle = 'rgba(148,163,184,0.55)';
    ctx.fillStyle = 'rgba(148,163,184,0.9)';
    ctx.lineWidth = 1;
    ctx.font = '10px sans-serif';
    for (let mm = 0; mm <= 5000; mm += 1000) {
      const ratio = mm / 5000;
      const y = pondBottom - pondH * ratio;
      ctx.beginPath();
      ctx.moveTo(leftX - 12, y);
      ctx.lineTo(leftX, y);
      ctx.moveTo(rightX + rightW, y);
      ctx.lineTo(rightX + rightW + 12, y);
      ctx.stroke();
      const label = `${mm / 1000}m`;
      ctx.fillText(label, leftX - 34, y + 3);
      ctx.fillText(label, rightX + rightW + 14, y + 3);
    }

    const mmToRatio = (mm) => clamp((Number(mm) || 0) / 5000, 0, 1);
    const lRatio = outer == null ? 0 : mmToRatio(outer);
    const rRatio = inner == null ? 0 : mmToRatio(inner);
    const lLevelH = pondH * lRatio;
    const rLevelH = pondH * rRatio;

    const waterOuter = ctx.createLinearGradient(0, pondTop, 0, pondBottom);
    waterOuter.addColorStop(0, '#60a5fa');
    waterOuter.addColorStop(1, '#1d4ed8');
    const waterInner = ctx.createLinearGradient(0, pondTop, 0, pondBottom);
    waterInner.addColorStop(0, '#67e8f9');
    waterInner.addColorStop(1, '#0e7490');

    if (outer != null) {
      ctx.fillStyle = waterOuter;
      ctx.fillRect(leftX + 2, pondBottom - lLevelH, leftW - 4, lLevelH);
    }
    if (inner != null) {
      ctx.fillStyle = waterInner;
      ctx.fillRect(rightX + 2, pondBottom - rLevelH, rightW - 4, rLevelH);
    }
    const drawWave = (x, w, y, color, reverse) => {
      ctx.beginPath();
      ctx.moveTo(x, y);
      const amp = 2.8;
      for (let i = 0; i <= 14; i += 1) {
        const rx = i / 14;
        const px = x + w * rx;
        const phase = wavePhase + (reverse ? -1 : 1) * rx * Math.PI * 2.2;
        const py = y + Math.sin(phase) * amp;
        ctx.lineTo(px, py);
      }
      ctx.strokeStyle = color;
      ctx.lineWidth = 1.6;
      ctx.stroke();
    };
    if (outer != null && lLevelH > 3) {
      drawWave(leftX + 3, leftW - 6, pondBottom - lLevelH, 'rgba(186, 230, 253, 0.85)', false);
    }
    if (inner != null && rLevelH > 3) {
      drawWave(rightX + 3, rightW - 6, pondBottom - rLevelH, 'rgba(186, 230, 253, 0.85)', true);
    }

    const gateY = pondTop + 6 - pondH * 0.42 * gateRatio;
    let gateColor = '#f59e0b';
    if (gateRatio >= 0.95) gateColor = '#22c55e';
    else if (gateRatio <= 0.05) gateColor = '#fb7185';
    const plateX = gateX + 26;
    const plateW = gateW - 52;
    const plateH = pondH + 20;
    ctx.fillStyle = gateColor;
    ctx.globalAlpha = 0.16;
    ctx.fillRect(plateX, gateY, plateW, plateH);
    ctx.globalAlpha = 1;
    ctx.strokeStyle = gateColor;
    ctx.lineWidth = 3;
    ctx.strokeRect(plateX, gateY, plateW, plateH);

    const connected = deviceOnline && (delta != null) && Math.abs(delta) >= 20 && gateRatio > 0.12;
    if (connected) {
      // Gate opening joins both sides; fill gate slot with channel water.
      const chanH = Math.max(0, Math.min(lLevelH, rLevelH));
      if (chanH > 1) {
        const chanGrad = ctx.createLinearGradient(gateX, pondBottom - chanH, gateX, pondBottom);
        chanGrad.addColorStop(0, 'rgba(56,189,248,0.38)');
        chanGrad.addColorStop(1, 'rgba(14,116,144,0.30)');
        ctx.fillStyle = chanGrad;
        ctx.fillRect(gateX + 14, pondBottom - chanH, gateW - 28, chanH);
      }

      const y = pondBottom - Math.max(18, Math.min(lLevelH, rLevelH));
      const fromLeft = delta < 0;
      const ax = gateX - 34;
      const bx = gateX + gateW + 34;
      const color = Math.abs(delta) > 80 ? '#f59e0b' : '#22d3ee';
      ctx.strokeStyle = color;
      ctx.fillStyle = color;
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.moveTo(ax, y);
      ctx.lineTo(bx, y);
      ctx.stroke();

      ctx.font = '11px sans-serif';
      ctx.fillStyle = 'rgba(226,232,240,0.95)';
      ctx.fillText(fromLeft ? '流向: 外塘 -> 内塘' : '流向: 内塘 -> 外塘', gateX - 30, y - 12);

      // Animated flow chevrons.
      const dir = fromLeft ? 1 : -1;
      const span = bx - ax;
      const phase = Number.isFinite(this._flowPhase) ? this._flowPhase : 0;
      for (let i = 0; i < 5; i += 1) {
        let p = ((i / 5) + phase) % 1;
        if (dir < 0) p = 1 - p;
        const x = ax + p * span;
        const s = 7;
        ctx.beginPath();
        ctx.moveTo(x, y);
        ctx.lineTo(x - s * dir, y - s * 0.7);
        ctx.lineTo(x - s * dir, y + s * 0.7);
        ctx.closePath();
        ctx.fill();
      }
    }

    ctx.fillStyle = '#e2e8f0';
    ctx.font = '13px sans-serif';
    ctx.fillText('外塘', leftX + 8, 22);
    ctx.fillText('内塘', rightX + 8, 22);
    ctx.fillText('水闸', gateX + 18, H - 10);
    ctx.font = '11px sans-serif';
    ctx.fillStyle = 'rgba(226,232,240,0.9)';
    ctx.fillText(`开度 ${Math.round(gateRatio * 100)}%`, gateX + 8, 36);
  },

  goDeviceConfig() {
    wx.navigateTo({ url: '/pages/device-config/index' });
  },

  goReplay() {
    wx.navigateTo({ url: '/pages/replay/index' });
  },

  goAdmin() {
    wx.navigateTo({ url: '/pages/admin/index' });
  },

  async logout() {
    try { await api.requestJSON('/api/auth/logout', { method: 'POST' }); } catch (e) {}
    api.setCookieRaw('');
    getApp().globalData.user = null;
    wx.reLaunch({ url: '/pages/login/index' });
  }
});
