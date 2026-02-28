const api = require('../../utils/api');
const fmt = require('../../utils/format');

const MODE_OPTIONS = [
  { value: 'mixed', label: '混合(推荐)' },
  { value: 'daily', label: '仅定时' },
  { value: 'cycle', label: '仅循环' },
  { value: 'leveldiff', label: '仅水位差' }
];

const STATE_OPTIONS = [
  { value: 'open', label: '开闸' },
  { value: 'close', label: '关闸' }
];

const DOW_OPTIONS = ['一', '二', '三', '四', '五', '六', '日'];

function num(v, defVal) {
  const n = Number(v);
  return Number.isFinite(n) ? n : defVal;
}

function clamp(v, a, b) {
  return Math.min(b, Math.max(a, v));
}

function hhmmToMs(v) {
  const s = String(v || '').trim();
  const m = /^(\d{1,2}):(\d{1,2})$/.exec(s);
  if (!m) return 0;
  const h = clamp(num(m[1], 0), 0, 23);
  const mm = clamp(num(m[2], 0), 0, 59);
  return (h * 3600 + mm * 60) * 1000;
}

function msToHHMM(v) {
  const ms = Math.max(0, num(v, 0));
  const day = 24 * 3600 * 1000;
  const n = ((ms % day) + day) % day;
  const h = Math.floor(n / 3600000);
  const m = Math.floor((n % 3600000) / 60000);
  const p2 = (x) => (x < 10 ? `0${x}` : `${x}`);
  return `${p2(h)}:${p2(m)}`;
}

function pickDeviceId() {
  return getApp().globalData.currentDeviceId || '';
}

function withDev(path) {
  const q = api.buildQuery({ device_id: pickDeviceId() });
  return `${path}${q}`;
}

function migrate(raw) {
  const out = Object.assign({ tz_offset_ms: 28800000, mode: 'mixed', daily: [], cycle: [], leveldiff: [] }, raw || {});
  if (!Number.isFinite(num(out.tz_offset_ms, NaN))) out.tz_offset_ms = 28800000;
  if (!out.mode) out.mode = 'mixed';

  out.daily = Array.isArray(out.daily) ? out.daily.slice(0, 8) : [];
  out.daily = out.daily.map((r) => {
    const rr = Object.assign({ en: false, open_en: true, close_en: true, open_ms: 28800000, close_ms: 32400000, dow_mask: 127 }, r || {});
    if (rr.open_ms == null && rr.open) rr.open_ms = hhmmToMs(rr.open);
    if (rr.close_ms == null && rr.close) rr.close_ms = hhmmToMs(rr.close);
    rr.open_ms = num(rr.open_ms, 28800000);
    rr.close_ms = num(rr.close_ms, 32400000);
    rr.dow_mask = num(rr.dow_mask, 127) & 127;
    return rr;
  });

  out.cycle = Array.isArray(out.cycle) ? out.cycle.slice(0, 5) : [];
  let cycleSeen = false;
  out.cycle = out.cycle.map((r) => {
    const rr = Object.assign({ en: false, steps: [] }, r || {});
    const en = !!rr.en && !cycleSeen;
    if (en) cycleSeen = true;
    rr.steps = Array.isArray(rr.steps) ? rr.steps.slice(0, 10) : [];
    rr.steps = rr.steps.map((st) => {
      const s = Object.assign({ state: 'open', dur_ms: 3600000 }, st || {});
      if (s.dur_ms == null && s.min != null) s.dur_ms = num(s.min, 60) * 60000;
      if (s.dur_ms == null && s.ms != null) s.dur_ms = num(s.ms, 60000);
      s.dur_ms = Math.max(1, num(s.dur_ms, 60000));
      s.state = s.state === 'close' ? 'close' : 'open';
      return s;
    });
    return { en, steps: rr.steps };
  });

  out.leveldiff = Array.isArray(out.leveldiff) ? out.leveldiff.slice(0, 4) : [];
  let ldSeen = false;
  out.leveldiff = out.leveldiff.map((r) => {
    const rr = Object.assign({ en: false, open_mm: -1, close_mm: 0 }, r || {});
    const en = !!rr.en && !ldSeen;
    if (en) ldSeen = true;
    return {
      en,
      open_mm: num(rr.open_mm, -1),
      close_mm: num(rr.close_mm, 0)
    };
  });

  return out;
}

function dailyToUi(r, idx) {
  const dow = [];
  for (let i = 0; i < 7; i += 1) dow.push(!!(r.dow_mask & (1 << i)));
  return {
    _id: `d${idx}`,
    en: !!r.en,
    open_en: r.open_en !== false,
    close_en: r.close_en !== false,
    open_hhmm: msToHHMM(r.open_ms),
    close_hhmm: msToHHMM(r.close_ms),
    dow
  };
}

function cycleStepToUi(st, idx) {
  const durMs = Math.max(1, num(st.dur_ms, 60000));
  const totalMin = Math.round(durMs / 60000);
  const durH = Math.floor(totalMin / 60);
  const durM = totalMin % 60;
  const state = st.state === 'close' ? 'close' : 'open';
  return {
    _id: `s${idx}`,
    state,
    stateIndex: state === 'close' ? 1 : 0,
    dur_h: durH,
    dur_m: durM
  };
}

function cycleToUi(r, idx) {
  return {
    _id: `c${idx}`,
    en: !!r.en,
    steps: (r.steps || []).map((st, si) => cycleStepToUi(st, si))
  };
}

function ldToUi(r, idx) {
  return {
    _id: `l${idx}`,
    en: !!r.en,
    open_mm: num(r.open_mm, -1),
    close_mm: num(r.close_mm, 0)
  };
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
    stateOptions: STATE_OPTIONS,
    dowOptions: DOW_OPTIONS,
    modeIndex: 0,
    tzHours: 8,
    dailyRules: [],
    cycleRules: [],
    leveldiffRules: []
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

  applyCfgToForm(cfg, msg) {
    const c = migrate(cfg);
    const modeIndex = Math.max(0, MODE_OPTIONS.findIndex((m) => m.value === c.mode));
    const tzHours = Math.round(num(c.tz_offset_ms, 28800000) / 3600000);
    const dailyRules = c.daily.map((r, i) => dailyToUi(r, i));
    const cycleRules = c.cycle.map((r, i) => cycleToUi(r, i));
    const leveldiffRules = c.leveldiff.map((r, i) => ldToUi(r, i));
    this.setData({
      modeIndex,
      tzHours,
      dailyRules,
      cycleRules,
      leveldiffRules,
      raw: JSON.stringify(c, null, 2),
      msg: msg || ''
    });
  },

  buildCfgFromForm() {
    const mode = MODE_OPTIONS[this.data.modeIndex] ? MODE_OPTIONS[this.data.modeIndex].value : 'mixed';
    const tzH = clamp(num(this.data.tzHours, 8), -12, 14);

    const daily = (this.data.dailyRules || []).map((r) => {
      let dowMask = 0;
      const arr = Array.isArray(r.dow) ? r.dow : [];
      for (let i = 0; i < 7; i += 1) {
        if (arr[i]) dowMask |= (1 << i);
      }
      return {
        en: !!r.en,
        open_en: r.open_en !== false,
        close_en: r.close_en !== false,
        open_ms: hhmmToMs(r.open_hhmm),
        close_ms: hhmmToMs(r.close_hhmm),
        dow_mask: dowMask
      };
    });

    let cycleSeen = false;
    const cycle = (this.data.cycleRules || []).map((r) => {
      const steps = (r.steps || []).map((st) => {
        const h = clamp(num(st.dur_h, 0), 0, 999);
        const m = clamp(num(st.dur_m, 0), 0, 59);
        const durMs = Math.max(1, (h * 3600 + m * 60) * 1000);
        const state = st.state === 'close' ? 'close' : 'open';
        return { state, dur_ms: durMs };
      });
      const en = !!r.en && !cycleSeen;
      if (en) cycleSeen = true;
      return { en, steps };
    });

    let ldSeen = false;
    const leveldiff = (this.data.leveldiffRules || []).map((r) => {
      const en = !!r.en && !ldSeen;
      if (en) ldSeen = true;
      return {
        en,
        open_mm: num(r.open_mm, -1),
        close_mm: num(r.close_mm, 0)
      };
    });

    return migrate({
      tz_offset_ms: tzH * 3600000,
      mode,
      daily,
      cycle,
      leveldiff
    });
  },

  syncRawFromForm(msg) {
    const c = this.buildCfgFromForm();
    this.setData({
      raw: JSON.stringify(c, null, 2),
      msg: msg || this.data.msg || ''
    });
  },

  async loadCfg() {
    this.setData({ msg: '加载中…' });
    try {
      let raw = '';
      const cfgUrl = withDev('/api/config');
      const cacheUrl = `${cfgUrl}${cfgUrl.includes('?') ? '&' : '?'}source=cache`;
      try {
        raw = await api.requestText(cacheUrl);
      } catch (e) {
        raw = await api.requestText(cfgUrl);
      }
      const parsed = fmt.safeParseJSON(raw, {});
      this.applyCfgToForm(parsed, '');
    } catch (e) {
      this.setData({ msg: `加载失败：${e.message}` });
    }
  },

  onModeChange(e) {
    this.setData({ modeIndex: Number(e.detail.value) || 0 });
    this.syncRawFromForm();
  },

  onTzInput(e) {
    this.setData({ tzHours: num(e.detail.value, 8) });
    this.syncRawFromForm();
  },

  addDaily() {
    const list = (this.data.dailyRules || []).slice();
    if (list.length >= 8) {
      this.setData({ msg: '定时最多 8 组' });
      return;
    }
    list.push(dailyToUi({ en: true, open_en: true, close_en: true, open_ms: hhmmToMs('08:00'), close_ms: hhmmToMs('09:00'), dow_mask: 127 }, list.length));
    this.setData({ dailyRules: list, msg: '' });
    this.syncRawFromForm();
  },

  delDaily(e) {
    const idx = Number(e.currentTarget.dataset.i);
    const list = (this.data.dailyRules || []).slice();
    if (!Number.isFinite(idx) || idx < 0 || idx >= list.length) return;
    list.splice(idx, 1);
    this.setData({ dailyRules: list, msg: '' });
    this.syncRawFromForm();
  },

  onDailyFlag(e) {
    const idx = Number(e.currentTarget.dataset.i);
    const key = String(e.currentTarget.dataset.k || '');
    const list = (this.data.dailyRules || []).slice();
    if (!list[idx]) return;
    list[idx][key] = !!e.detail.value;
    this.setData({ dailyRules: list, msg: '' });
    this.syncRawFromForm();
  },

  onDailyTime(e) {
    const idx = Number(e.currentTarget.dataset.i);
    const key = String(e.currentTarget.dataset.k || '');
    const list = (this.data.dailyRules || []).slice();
    if (!list[idx]) return;
    list[idx][key] = String(e.detail.value || '');
    this.setData({ dailyRules: list, msg: '' });
    this.syncRawFromForm();
  },

  onDailyDowToggle(e) {
    const ri = Number(e.currentTarget.dataset.ri);
    const di = Number(e.currentTarget.dataset.di);
    const list = (this.data.dailyRules || []).slice();
    if (!list[ri]) return;
    if (!Array.isArray(list[ri].dow)) list[ri].dow = [true, true, true, true, true, true, true];
    list[ri].dow[di] = !list[ri].dow[di];
    this.setData({ dailyRules: list, msg: '' });
    this.syncRawFromForm();
  },

  addCycleRule() {
    const list = (this.data.cycleRules || []).slice();
    if (list.length >= 5) {
      this.setData({ msg: '循环最多 5 组' });
      return;
    }
    const anyOn = list.some((r) => r && r.en);
    list.push({
      _id: `c${Date.now()}`,
      en: !anyOn,
      steps: [
        { _id: `s${Date.now()}_1`, state: 'open', stateIndex: 0, dur_h: 8, dur_m: 0 },
        { _id: `s${Date.now()}_2`, state: 'close', stateIndex: 1, dur_h: 3, dur_m: 0 }
      ]
    });
    this.setData({ cycleRules: list, msg: '' });
    this.syncRawFromForm();
  },

  delCycleRule(e) {
    const ri = Number(e.currentTarget.dataset.ri);
    const list = (this.data.cycleRules || []).slice();
    if (!list[ri]) return;
    list.splice(ri, 1);
    this.setData({ cycleRules: list, msg: '' });
    this.syncRawFromForm();
  },

  onCycleEnable(e) {
    const ri = Number(e.currentTarget.dataset.ri);
    const en = !!e.detail.value;
    const list = (this.data.cycleRules || []).slice();
    if (!list[ri]) return;
    if (en) {
      list.forEach((r, i) => { r.en = i === ri; });
    } else {
      list[ri].en = false;
    }
    this.setData({ cycleRules: list, msg: '' });
    this.syncRawFromForm();
  },

  addCycleStep(e) {
    const ri = Number(e.currentTarget.dataset.ri);
    const list = (this.data.cycleRules || []).slice();
    if (!list[ri]) return;
    const steps = Array.isArray(list[ri].steps) ? list[ri].steps.slice() : [];
    if (steps.length >= 10) {
      this.setData({ msg: '每组循环最多 10 段' });
      return;
    }
    steps.push({ _id: `s${Date.now()}`, state: 'open', stateIndex: 0, dur_h: 1, dur_m: 0 });
    list[ri].steps = steps;
    this.setData({ cycleRules: list, msg: '' });
    this.syncRawFromForm();
  },

  delCycleStep(e) {
    const ri = Number(e.currentTarget.dataset.ri);
    const si = Number(e.currentTarget.dataset.si);
    const list = (this.data.cycleRules || []).slice();
    if (!list[ri] || !Array.isArray(list[ri].steps) || !list[ri].steps[si]) return;
    list[ri].steps.splice(si, 1);
    this.setData({ cycleRules: list, msg: '' });
    this.syncRawFromForm();
  },

  onCycleStepState(e) {
    const ri = Number(e.currentTarget.dataset.ri);
    const si = Number(e.currentTarget.dataset.si);
    const idx = Number(e.detail.value) || 0;
    const list = (this.data.cycleRules || []).slice();
    if (!list[ri] || !list[ri].steps || !list[ri].steps[si]) return;
    const st = list[ri].steps[si];
    st.stateIndex = idx;
    st.state = idx === 1 ? 'close' : 'open';
    this.setData({ cycleRules: list, msg: '' });
    this.syncRawFromForm();
  },

  onCycleStepDur(e) {
    const ri = Number(e.currentTarget.dataset.ri);
    const si = Number(e.currentTarget.dataset.si);
    const key = String(e.currentTarget.dataset.k || '');
    const list = (this.data.cycleRules || []).slice();
    if (!list[ri] || !list[ri].steps || !list[ri].steps[si]) return;
    list[ri].steps[si][key] = num(e.detail.value, 0);
    this.setData({ cycleRules: list, msg: '' });
    this.syncRawFromForm();
  },

  addLeveldiff() {
    const list = (this.data.leveldiffRules || []).slice();
    if (list.length >= 4) {
      this.setData({ msg: '水位差最多 4 组' });
      return;
    }
    const anyOn = list.some((r) => r && r.en);
    list.push({ _id: `l${Date.now()}`, en: !anyOn, open_mm: -1, close_mm: 0 });
    this.setData({ leveldiffRules: list, msg: '' });
    this.syncRawFromForm();
  },

  delLeveldiff(e) {
    const i = Number(e.currentTarget.dataset.i);
    const list = (this.data.leveldiffRules || []).slice();
    if (!list[i]) return;
    list.splice(i, 1);
    this.setData({ leveldiffRules: list, msg: '' });
    this.syncRawFromForm();
  },

  onLeveldiffEnable(e) {
    const i = Number(e.currentTarget.dataset.i);
    const en = !!e.detail.value;
    const list = (this.data.leveldiffRules || []).slice();
    if (!list[i]) return;
    if (en) {
      list.forEach((r, idx) => { r.en = idx === i; });
    } else {
      list[i].en = false;
    }
    this.setData({ leveldiffRules: list, msg: '' });
    this.syncRawFromForm();
  },

  onLeveldiffInput(e) {
    const i = Number(e.currentTarget.dataset.i);
    const key = String(e.currentTarget.dataset.k || '');
    const list = (this.data.leveldiffRules || []).slice();
    if (!list[i]) return;
    list[i][key] = num(e.detail.value, key === 'open_mm' ? -1 : 0);
    this.setData({ leveldiffRules: list, msg: '' });
    this.syncRawFromForm();
  },

  onRawInput(e) {
    this.setData({ raw: e.detail.value || '' });
  },

  applyRaw() {
    try {
      const parsed = fmt.safeParseJSON(this.data.raw || '{}', null);
      if (!parsed || typeof parsed !== 'object') {
        this.setData({ msg: 'JSON 无效，无法应用' });
        return;
      }
      this.applyCfgToForm(parsed, 'JSON 已应用到表单');
    } catch (e) {
      this.setData({ msg: `应用失败：${e.message}` });
    }
  },

  formatJson() {
    try {
      const parsed = fmt.safeParseJSON(this.data.raw || '{}', null);
      if (!parsed || typeof parsed !== 'object') {
        this.setData({ msg: 'JSON 无效，无法格式化' });
        return;
      }
      const c = migrate(parsed);
      this.setData({
        raw: JSON.stringify(c, null, 2),
        msg: '已格式化'
      });
    } catch (e) {
      this.setData({ msg: `格式化失败：${e.message}` });
    }
  },

  async saveCfg() {
    this.setData({ saving: true, msg: '' });
    try {
      const body = this.buildCfgFromForm();
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
