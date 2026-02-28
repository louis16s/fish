const api = require('../../utils/api');
const fmt = require('../../utils/format');

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

Page({
  data: {
    userText: '--',
    deviceOptions: [],
    deviceIndex: 0,
    currentDeviceLabel: '--',
    mqttText: '--',
    mqttTagClass: '',
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
    logMsg: ''
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
      const j = await api.requestJSON(withDev('/api/state'));
      const t = j.telemetry || {};
      const s1 = t.sensor1 || {};
      const s2 = t.sensor2 || {};
      const inner = s1.valid ? Number(s1.mm) : null;
      const outer = s2.valid ? Number(s2.mm) : null;
      const delta = (inner != null && outer != null) ? (inner - outer) : null;
      const fw = (t.fw && t.fw.current) ? t.fw.current : (t.fw_version || t.fw || '--');
      const auto = t.auto_latched ? '锁定关' : (t.auto_gate ? '开' : '关');

      this.setData({
        mqttText: j.mqtt_connected ? '已连接' : '未连接',
        mqttTagClass: j.mqtt_connected ? 'tag-good' : 'tag-bad',
        lastTelemetryAt: fmt.fmtDateTime(j.last_telemetry_at || 0),
        fwVersion: fw || '--',
        innerText: inner == null ? '--' : `${inner} mm`,
        outerText: outer == null ? '--' : `${outer} mm`,
        deltaText: delta == null ? '--' : `${delta} mm`,
        gateText: fmt.gateStateText(t.gate_state),
        autoText: auto,
        alarmText: fmt.alarmText(t.alarm)
      });
    } catch (e) {
      this.setData({ mqttText: '异常', mqttTagClass: 'tag-bad' });
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
