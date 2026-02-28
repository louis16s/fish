const api = require('../../utils/api');
const fmt = require('../../utils/format');

const MODE_OPTIONS = [
  { value: 'mixed', label: '混合(推荐)' },
  { value: 'daily', label: '仅定时' },
  { value: 'cycle', label: '仅循环' },
  { value: 'leveldiff', label: '仅水位差' }
];

function pickDeviceId() {
  return getApp().globalData.currentDeviceId || '';
}

function withDev(path) {
  const q = api.buildQuery({ device_id: pickDeviceId() });
  return `${path}${q}`;
}

function cfgMigrate(raw) {
  const out = Object.assign({ tz_offset_ms: 28800000, mode: 'mixed', daily: [], cycle: [], leveldiff: [] }, raw || {});
  if (!Number.isFinite(Number(out.tz_offset_ms))) out.tz_offset_ms = 28800000;
  if (!out.mode) out.mode = 'mixed';
  if (!Array.isArray(out.daily)) out.daily = [];
  if (!Array.isArray(out.cycle)) out.cycle = [];
  if (!Array.isArray(out.leveldiff)) out.leveldiff = [];
  return out;
}

Page({
  data: {
    deviceOptions: [],
    deviceIndex: 0,
    currentDeviceLabel: '--',
    raw: '{}',
    msg: '',
    saving: false,
    modeOptions: MODE_OPTIONS,
    modeIndex: 0,
    tzHours: 8
  },

  onShow() {
    this.bootstrap();
  },

  async bootstrap() {
    try {
      await api.requestJSON('/api/auth/me');
    } catch (e) {
      wx.reLaunch({ url: '/pages/login/index' });
      return;
    }
    await this.loadDevices();
    await this.loadCfg();
  },

  async loadDevices() {
    try {
      const j = await api.requestJSON('/api/devices');
      const list = Array.isArray(j.devices) ? j.devices : [];
      const opts = list.map((x) => ({ label: x.device_id || '', value: x.device_id || '' }));
      let did = pickDeviceId();
      if (!did && opts.length) did = opts[0].value;
      const idx = Math.max(0, opts.findIndex((x) => x.value === did));
      const cur = opts[idx] || { label: '(暂无设备)', value: '' };
      getApp().globalData.currentDeviceId = cur.value || '';
      wx.setStorageSync('current_device_id', cur.value || '');
      this.setData({ deviceOptions: opts, deviceIndex: idx, currentDeviceLabel: cur.label });
    } catch (e) {
      this.setData({ currentDeviceLabel: '(加载失败)' });
    }
  },

  syncQuickFromRaw(rawObj) {
    const cfg = cfgMigrate(rawObj);
    const modeIndex = Math.max(0, MODE_OPTIONS.findIndex((m) => m.value === cfg.mode));
    this.setData({
      modeIndex,
      tzHours: Math.round(Number(cfg.tz_offset_ms || 28800000) / 3600000)
    });
  },

  applyQuickToRaw(rawObj) {
    const out = cfgMigrate(rawObj);
    const mode = MODE_OPTIONS[this.data.modeIndex] ? MODE_OPTIONS[this.data.modeIndex].value : 'mixed';
    out.mode = mode;
    out.tz_offset_ms = Number(this.data.tzHours || 8) * 3600000;
    return out;
  },

  async loadCfg() {
    this.setData({ msg: '加载中…' });
    try {
      let raw = '';
      const cacheUrl = `${withDev('/api/config')}${withDev('/api/config').includes('?') ? '&' : '?'}source=cache`;
      try {
        raw = await api.requestText(cacheUrl);
      } catch (e) {
        raw = await api.requestText(withDev('/api/config'));
      }
      const parsed = fmt.safeParseJSON(raw, {});
      const norm = cfgMigrate(parsed);
      this.syncQuickFromRaw(norm);
      this.setData({ raw: JSON.stringify(norm, null, 2), msg: '' });
    } catch (e) {
      this.setData({ msg: `加载失败：${e.message}` });
    }
  },

  onRawInput(e) {
    this.setData({ raw: e.detail.value || '' });
  },

  onModeChange(e) {
    this.setData({ modeIndex: Number(e.detail.value) || 0 });
  },

  onTzInput(e) {
    this.setData({ tzHours: Number(e.detail.value || 0) });
  },

  formatJson() {
    try {
      const p = fmt.safeParseJSON(this.data.raw || '{}', null);
      if (!p) {
        this.setData({ msg: 'JSON 格式错误，无法格式化' });
        return;
      }
      const normalized = this.applyQuickToRaw(p);
      this.setData({ raw: JSON.stringify(normalized, null, 2), msg: '已格式化' });
    } catch (e) {
      this.setData({ msg: `格式化失败：${e.message}` });
    }
  },

  async saveCfg() {
    this.setData({ saving: true, msg: '' });
    try {
      const parsed = fmt.safeParseJSON(this.data.raw || '{}', null);
      if (!parsed || typeof parsed !== 'object') {
        this.setData({ msg: '保存失败：JSON 无效', saving: false });
        return;
      }
      const body = this.applyQuickToRaw(parsed);
      await api.requestJSON(withDev('/api/config'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        data: body
      });
      this.setData({ raw: JSON.stringify(body, null, 2), msg: '已保存', saving: false });
    } catch (e) {
      this.setData({ msg: `保存失败：${e.message}`, saving: false });
    }
  },

  onDeviceChange(e) {
    const idx = Number(e.detail.value) || 0;
    const item = this.data.deviceOptions[idx];
    const did = item ? item.value : '';
    getApp().globalData.currentDeviceId = did;
    wx.setStorageSync('current_device_id', did || '');
    this.setData({ deviceIndex: idx, currentDeviceLabel: item ? item.label : '--' });
    this.loadCfg();
  },

  backPanel() {
    wx.navigateBack({ delta: 1 });
  }
});
