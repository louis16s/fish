const api = require('../../utils/api');
const fmt = require('../../utils/format');
const { RAW_BASE_URL } = require('../../utils/config');

function pickDeviceId() {
  const app = getApp();
  return app.globalData.currentDeviceId || '';
}

function withDev(path) {
  const did = pickDeviceId();
  const q = api.buildQuery({ device_id: did });
  return `${path}${q}`;
}

function withDevRaw(path) {
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
    gateStateNum: 0
  },

  onReady() {
    this.initSchematicCanvas();
  },

  onShow() {
    this.bootstrap();
    this.startPolling();
  },

  onHide() {
    this.stopPolling();
  },

  onUnload() {
    this.stopPolling();
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
      this.loadLog();
    }, 2500);
  },

  stopPolling() {
    if (this._tm) {
      clearInterval(this._tm);
      this._tm = 0;
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
      let j = null;
      let t = {};
      let lastAt = 0;
      let mqttConnected = false;

      try {
        if (!RAW_BASE_URL) throw new Error('raw_base_url_empty');
        const raw = await api.requestJSON(`${RAW_BASE_URL}${withDevRaw('/api/state')}`);
        t = raw.telemetry || {};
        lastAt = raw.last_telemetry_at || 0;
        mqttConnected = !!raw.mqtt_connected;
      } catch (eRaw) {
        j = await api.requestJSON(withDev('/api/state'));
        t = j.telemetry || {};
        lastAt = j.last_telemetry_at || 0;
        mqttConnected = !!j.mqtt_connected;
      }

      const s1 = t.sensor1 || {};
      const s2 = t.sensor2 || {};
      const inner = s1.valid ? Number(s1.mm) : null;
      const outer = s2.valid ? Number(s2.mm) : null;
      const delta = (inner != null && outer != null) ? (inner - outer) : null;
      const fw = (t.fw && t.fw.current) ? t.fw.current : (t.fw_version || t.fw || '--');
      const auto = t.auto_latched ? '锁定关' : (t.auto_gate ? '开' : '关');
      const gateStateNum = Number(t.gate_state || 0);
      const gateTagClass = gateStateNum === 1 ? 'tag-good' : (gateStateNum === 2 ? 'tag-bad' : 'tag-warn');
      const deltaTagClass = delta == null ? '' : (Math.abs(delta) > 80 ? 'tag-warn' : 'tag-good');
      const now = Date.now();
      const ageMs = lastAt > 0 ? Math.max(0, now - Number(lastAt || 0)) : 9e9;
      const deviceOnline = ageMs <= 15000;

      this.setData({
        mqttText: mqttConnected ? '已连接' : '未连接',
        mqttTagClass: mqttConnected ? 'tag-good' : 'tag-bad',
        serverMqttText: mqttConnected ? '服务器 已连接' : '服务器 未连接',
        serverMqttTagClass: mqttConnected ? 'tag-good' : 'tag-bad',
        deviceOnlineText: deviceOnline ? '状态 在线' : '状态 离线',
        deviceOnlineTagClass: deviceOnline ? 'tag-good' : 'tag-bad',
        lastTelemetryAt: fmt.fmtDateTime(lastAt || 0),
        fwVersion: fw || '--',
        innerText: inner == null ? '--' : `${inner} mm`,
        outerText: outer == null ? '--' : `${outer} mm`,
        deltaText: delta == null ? '--' : `${delta} mm`,
        gateText: fmt.gateStateText(t.gate_state),
        autoText: auto,
        alarmText: fmt.alarmText(t.alarm),
        gateTagClass,
        deltaTagClass,
        innerMm: inner,
        outerMm: outer,
        deltaMm: delta,
        gateStateNum
      });
      this.drawSchematic();
    } catch (e) {
      this.setData({
        mqttText: '异常',
        mqttTagClass: 'tag-bad',
        serverMqttText: '服务器 状态未知',
        serverMqttTagClass: 'tag-bad',
        deviceOnlineText: '状态 未知',
        deviceOnlineTagClass: 'tag-bad'
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
      this.setData({ cmdMsg: `已发送：${cmd}` });
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
      this.setData({ logText: text || '(空)', logMsg: msg });
    } catch (e) {
      this.setData({ logMsg: `读取失败：${e.message}` });
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

  drawSchematic() {
    const ctx = this._schemCtx;
    const W = this._schemW;
    const H = this._schemH;
    if (!ctx || !W || !H) return;

    const inner = this.data.innerMm;
    const outer = this.data.outerMm;
    const delta = this.data.deltaMm;
    const gateState = Number(this.data.gateStateNum || 0);

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

    let gateY = pondTop + 8;
    let gateColor = '#f59e0b';
    if (gateState === 1) {
      gateY = pondTop - pondH * 0.42;
      gateColor = '#22c55e';
    } else if (gateState === 2) {
      gateY = pondTop + 6;
      gateColor = '#fb7185';
    }
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

    if (delta != null && Math.abs(delta) >= 20) {
      const y = pondBottom - Math.max(18, Math.min(lLevelH, rLevelH));
      const fromLeft = delta < 0;
      const ax = fromLeft ? gateX - 30 : gateX + gateW + 30;
      const bx = fromLeft ? gateX + gateW + 30 : gateX - 30;
      const color = Math.abs(delta) > 80 ? '#f59e0b' : '#22d3ee';
      ctx.strokeStyle = color;
      ctx.fillStyle = color;
      ctx.lineWidth = 4;
      ctx.beginPath();
      ctx.moveTo(ax, y);
      ctx.lineTo(bx, y);
      ctx.stroke();
      const dir = fromLeft ? 1 : -1;
      ctx.beginPath();
      ctx.moveTo(bx, y);
      ctx.lineTo(bx - 10 * dir, y - 7);
      ctx.lineTo(bx - 10 * dir, y + 7);
      ctx.closePath();
      ctx.fill();
    }

    ctx.fillStyle = '#e2e8f0';
    ctx.font = '13px sans-serif';
    ctx.fillText('外塘', leftX + 8, 22);
    ctx.fillText('内塘', rightX + 8, 22);
    ctx.fillText('水闸', gateX + 18, H - 10);
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
