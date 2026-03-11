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

function clamp(n, a, b) {
  return Math.min(b, Math.max(a, n));
}

function num(v, defVal) {
  const n = Number(v);
  return Number.isFinite(n) ? n : defVal;
}

function modeLabel(mode) {
  if (mode === 'daily') return '仅定时';
  if (mode === 'cycle') return '仅循环';
  if (mode === 'leveldiff') return '仅水位差';
  return '混合(推荐)';
}

function tzLabelFromHours(tzH) {
  const h = Number.isFinite(Number(tzH)) ? Number(tzH) : 8;
  return `UTC${h >= 0 ? '+' : ''}${h}`;
}

function msToHHMMMaybe(v) {
  const ms = num(v, null);
  if (ms == null) return '--';
  const day = 24 * 3600000;
  const n = ((ms % day) + day) % day;
  const h = Math.floor(n / 3600000);
  const m = Math.floor((n % 3600000) / 60000);
  return `${fmt.pad2(h)}:${fmt.pad2(m)}`;
}

function durHM(v) {
  const totalMin = Math.round(Math.max(0, num(v, 0)) / 60000);
  const h = Math.floor(totalMin / 60);
  const m = totalMin % 60;
  return `${h}h${fmt.pad2(m)}m`;
}

function dowMaskText(mask) {
  const m = (Number.isFinite(Number(mask)) ? Number(mask) : 127) & 127;
  if (m === 127) return '';
  const days = ['一', '二', '三', '四', '五', '六', '日'];
  let text = '';
  for (let i = 0; i < 7; i += 1) {
    if (m & (1 << i)) text += days[i];
  }
  return text ? `周${text}` : '周(未选)';
}

function migrateConfig(raw) {
  const out = Object.assign({ tz_offset_ms: 28800000, mode: 'mixed', daily: [], cycle: [], leveldiff: [] }, raw || {});
  if (out.tz_offset_ms == null && out.tz_offset_s != null) out.tz_offset_ms = num(out.tz_offset_s, 28800) * 1000;
  out.tz_offset_ms = num(out.tz_offset_ms, 28800000);
  if (!out.mode) out.mode = 'mixed';

  out.daily = Array.isArray(out.daily) ? out.daily : [];
  out.daily = out.daily.map((r) => {
    const rr = Object.assign({ en: false, open_en: true, close_en: true, open_ms: null, close_ms: null, dow_mask: 127 }, r || {});
    if (rr.open_ms == null && typeof rr.open === 'string') {
      const m = /^(\d{1,2}):(\d{1,2})$/.exec(rr.open.trim());
      if (m) rr.open_ms = (clamp(num(m[1], 0), 0, 23) * 3600 + clamp(num(m[2], 0), 0, 59) * 60) * 1000;
    }
    if (rr.close_ms == null && typeof rr.close === 'string') {
      const m = /^(\d{1,2}):(\d{1,2})$/.exec(rr.close.trim());
      if (m) rr.close_ms = (clamp(num(m[1], 0), 0, 23) * 3600 + clamp(num(m[2], 0), 0, 59) * 60) * 1000;
    }
    if (rr.open_ms != null) rr.open_ms = num(rr.open_ms, null);
    if (rr.close_ms != null) rr.close_ms = num(rr.close_ms, null);
    rr.dow_mask = num(rr.dow_mask, 127) & 127;
    return rr;
  });

  out.cycle = Array.isArray(out.cycle) ? out.cycle : [];
  out.cycle = out.cycle.map((r) => {
    const rr = Object.assign({ en: false, steps: [] }, r || {});
    rr.steps = Array.isArray(rr.steps) ? rr.steps : [];
    rr.steps = rr.steps.map((st) => {
      const s = Object.assign({ state: 'open', dur_ms: null }, st || {});
      if (s.dur_ms == null && s.min != null) s.dur_ms = num(s.min, 60) * 60000;
      if (s.dur_ms == null && s.ms != null) s.dur_ms = num(s.ms, 60000);
      if (s.dur_ms != null) s.dur_ms = Math.max(1, num(s.dur_ms, 60000));
      s.state = s.state === 'close' ? 'close' : 'open';
      return s;
    });
    return rr;
  });

  out.leveldiff = Array.isArray(out.leveldiff) ? out.leveldiff : [];
  out.leveldiff = out.leveldiff.map((r) => {
    const rr = Object.assign({ en: false, open_mm: null, close_mm: null }, r || {});
    if (rr.open_mm != null) rr.open_mm = num(rr.open_mm, null);
    if (rr.close_mm != null) rr.close_mm = num(rr.close_mm, null);
    return rr;
  });
  return out;
}

function buildSummaryItems(cfg) {
  const daily = Array.isArray(cfg.daily) ? cfg.daily : [];
  const dailyOn = daily.filter((r) => r && r.en);
  const dailyItems = !daily.length
    ? [{ title: '定时', value: '未配置', on: false }]
    : (!dailyOn.length
      ? [{ title: '定时', value: '无（均未启用）', on: false }]
      : dailyOn.map((r, i) => {
        const parts = [];
        const dow = dowMaskText(r && r.dow_mask);
        if (dow) parts.push(dow);
        if (r.open_en !== false) parts.push(`开 ${msToHHMMMaybe(r.open_ms)}`);
        if (r.close_en !== false) parts.push(`关 ${msToHHMMMaybe(r.close_ms)}`);
        return { title: `定时 #${i + 1}`, value: `启用 | ${parts.join(' | ')}`, on: true };
      }));

  const cycle = Array.isArray(cfg.cycle) ? cfg.cycle : [];
  const cycleOn = cycle.filter((r) => r && r.en);
  const cycleItems = !cycle.length
    ? [{ title: '循环', value: '未配置', on: false }]
    : (!cycleOn.length
      ? [{ title: '循环', value: '无（均未启用）', on: false }]
      : cycleOn.map((r, i) => {
        const steps = Array.isArray(r.steps) ? r.steps : [];
        const seq = steps.map((st) => `${st && st.state === 'close' ? '关' : '开'} ${st && Number.isFinite(st.dur_ms) ? durHM(st.dur_ms) : '--'}`).join(' -> ');
        return { title: `循环 #${i + 1}`, value: `启用${seq ? ` | ${seq}` : ''}`, on: true };
      }));

  const ld = Array.isArray(cfg.leveldiff) ? cfg.leveldiff : [];
  const ldOn = ld.filter((r) => r && r.en);
  const leveldiffItems = !ld.length
    ? [{ title: '水位差', value: '未配置', on: false }]
    : (!ldOn.length
      ? [{ title: '水位差', value: '无（均未启用）', on: false }]
      : ldOn.map((r, i) => ({
        title: `水位差 #${i + 1}`,
        value: `启用 | 开阈值 ${Number.isFinite(r.open_mm) ? `${r.open_mm}mm` : '--'} | 关阈值 ${Number.isFinite(r.close_mm) ? `${r.close_mm}mm` : '--'}`,
        on: true
      })));

  return { dailyItems, cycleItems, leveldiffItems };
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
  if (t && typeof t.gate_position_open === 'boolean') {
    return t.gate_position_open ? 1 : 0;
  }
  return 0;
}

function gateAnimTargetFromTelemetry(t, gateStateNum) {
  const state = Number(gateStateNum || 0);
  if (state === 1) return 1;
  if (state === 2) return 0;
  return gateTargetFromTelemetry(t, gateStateNum);
}

function gateDisplayText(deviceOnline, gateStateNum, gateProgress) {
  if (!deviceOnline) return '离线';
  const state = Number(gateStateNum || 0);
  const progress = clamp(Number(gateProgress || 0), 0, 100);
  if (state === 1 && progress < 100) return '开闸中';
  if (state === 2 && progress > 0) return '关闸中';
  return progress >= 50 ? '已开' : '已关';
}

function gateTagClassFor(deviceOnline, gateStateNum, gateProgress) {
  if (!deviceOnline) return 'tag-bad';
  const state = Number(gateStateNum || 0);
  const progress = clamp(Number(gateProgress || 0), 0, 100);
  if ((state === 1 && progress < 100) || (state === 2 && progress > 0)) return 'tag-warn';
  return progress >= 50 ? 'tag-good' : 'tag-bad';
}

const CMD_LABELS = {
  gate_open: '开闸',
  gate_close: '关闸',
  gate_stop: '停止',
  auto_on: '开启自动',
  auto_off: '手动接管',
  auto_latch_off: '锁定关闭自动',
  manual_end: '恢复自动'
};

Page({
  data: {
    userText: '--',
    isAdmin: false,
    deviceOptions: [],
    deviceIndex: 0,
    currentDeviceLabel: '--',
    serverMqttText: '--',
    serverMqttTagClass: '',
    deviceOnlineText: '--',
    deviceOnlineTagClass: '',
    lastTelemetryAt: '--',
    innerText: '--',
    outerText: '--',
    innerStatusText: '--',
    outerStatusText: '--',
    deltaText: '--',
    gateText: '--',
    autoText: '--',
    autoToggleText: '关闭自动',
    autoToggleClass: 'ctl-neutral',
    alarmText: '--',
    cmdLoading: false,
    cmdMsg: '',
    cfgSummary: { mode: '--', tz: '--', dailyItems: [], cycleItems: [], leveldiffItems: [], msg: '' },
    logTab: 'error',
    logText: '加载中…',
    logMsg: '',
    schematicMsg: '',
    gateProgress: 0,
    pageStatusClass: 'is-warn',
    canAdminOperate: false,
    deviceOnline: false
  },

  onReady() {
    this.initSchematicCanvas();
    this.initLogObserver();
    this.startSchematicAnim();
  },

  syncGateProgress(force) {
    const gateState = Number(this._gateStateNum || 0);
    const fallback = gateState === 1 ? 1 : 0;
    const ratio = clamp(Number.isFinite(this._gateRatioCurrent) ? this._gateRatioCurrent : fallback, 0, 1);
    const gateProgress = Math.round(ratio * 100);
    if (!force && gateProgress === this.data.gateProgress) return;
    const gateText = gateDisplayText(this.data.deviceOnline, gateState, gateProgress);
    const patch = { gateProgress, gateText };
    if (this.data.cmdMsg === '已发送：关闸' && gateProgress === 0) patch.cmdMsg = '';
    if (this.data.cmdMsg === '已发送：开闸' && gateProgress === 100) patch.cmdMsg = '';
    this.setData(patch);
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
    this.stopLogObserver();
  },

  async bootstrap() {
    try {
      const me = await api.requestJSON('/api/auth/me');
      const app = getApp();
      app.globalData.user = me.user || null;
      const role = String((me.user && me.user.role) || 'user').toLowerCase();
      const isAdmin = role === 'admin';
      this.setData({
        userText: `用户 ${me.user.username} | ${me.user.role}`,
        isAdmin
      });
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
      if (this._logVisible) this.loadLog();
    }, LOG_POLL_MS);
  },

  stopLogPolling() {
    if (this._logTm) {
      clearInterval(this._logTm);
      this._logTm = 0;
    }
  },

  initLogObserver() {
    if (this._logObserverInited) return;
    this._logObserverInited = true;
    this._logVisible = false;
    if (typeof this.createIntersectionObserver !== 'function') {
      // Fallback: old base library, keep old behavior.
      this._logVisible = true;
      this.loadLog(true);
      return;
    }
    const obs = this.createIntersectionObserver({ thresholds: [0.02] });
    obs.relativeToViewport().observe('#logCard', (res) => {
      const visible = !!(res && Number(res.intersectionRatio) > 0);
      const wasVisible = !!this._logVisible;
      this._logVisible = visible;
      if (visible && !wasVisible) this.loadLog(true);
    });
    this._logObserver = obs;
  },

  stopLogObserver() {
    if (this._logObserver && typeof this._logObserver.disconnect === 'function') {
      this._logObserver.disconnect();
    }
    this._logObserver = null;
    this._logObserverInited = false;
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
      const autoGate = !!t.auto_gate;
      const autoLatched = !!t.auto_latched;
      this._autoLatched = autoLatched;
      const auto = autoLatched ? '锁定关闭' : (autoGate ? '已启用' : '已关闭');
      const gateStateNum = Number(t.gate_state || 0);
      this._gateStateNum = gateStateNum;
      const gateTarget = gateAnimTargetFromTelemetry(t, gateStateNum);
      const ctrl = t && t.ctrl ? t.ctrl : null;
      const actionS = Number(ctrl && ctrl.action_s);
      this._gateTravelS = (Number.isFinite(actionS) && actionS > 0) ? actionS : 10;
      this._gateRatioTarget = gateTarget;
      if (!Number.isFinite(this._gateRatioCurrent)) this._gateRatioCurrent = gateTarget;
      this._innerMm = inner;
      this._outerMm = outer;
      this._deltaMm = delta;
      const now = Date.now();
      const ageMs = lastAt > 0 ? Math.max(0, now - Number(lastAt || 0)) : 9e9;
      const deviceOnline = ageMs <= 15000;
      const gateProgress = Math.round(100 * clamp(Number.isFinite(this._gateRatioCurrent) ? this._gateRatioCurrent : gateTarget, 0, 1));
      const canOperate = !!(mqttConnected && deviceOnline);
      const canAdminOperate = !!(canOperate && this.data.isAdmin);
      const gateText = gateDisplayText(deviceOnline, gateStateNum, gateProgress);

      this.setData({
        serverMqttText: mqttConnected ? '服务器 已连接' : '服务器 未连接',
        serverMqttTagClass: mqttConnected ? 'tag-good' : 'tag-bad',
        deviceOnlineText: deviceOnline ? '设备状态 在线' : '设备状态 离线',
        deviceOnlineTagClass: deviceOnline ? 'tag-good' : 'tag-bad',
        deviceOnline,
        lastTelemetryAt: fmt.fmtDateTime(lastAt || 0),
        innerText: inner == null ? '--' : `${inner} mm`,
        outerText: outer == null ? '--' : `${outer} mm`,
        innerStatusText: inner == null ? '--' : `${(inner / 1000).toFixed(3)} m`,
        outerStatusText: outer == null ? '--' : `${(outer / 1000).toFixed(3)} m`,
        deltaText: delta == null ? '--' : `${delta} mm`,
        gateText,
        autoText: auto,
        autoToggleText: autoLatched ? '开启自动' : '关闭自动',
        autoToggleClass: autoLatched ? 'ctl-primary' : 'ctl-neutral',
        alarmText: fmt.alarmText(t.alarm),
        gateProgress,
        canAdminOperate,
        pageStatusClass: canOperate ? 'is-good' : 'is-warn'
      });
      this.syncGateProgress(true);
      this.drawSchematic();
    } catch (e) {
      this.setData({
        serverMqttText: '服务器 状态未知',
        serverMqttTagClass: 'tag-bad',
        deviceOnlineText: '设备状态 未知',
        deviceOnlineTagClass: 'tag-bad',
        deviceOnline: false,
        autoToggleText: '关闭自动',
        autoToggleClass: 'ctl-neutral',
        gateProgress: 0,
        canAdminOperate: false,
        pageStatusClass: 'is-warn'
      });
      this._autoLatched = false;
      this._innerMm = null;
      this._outerMm = null;
      this._deltaMm = null;
      this._gateStateNum = 0;
    }
  },

  async sendCmd(cmd, opts) {
    if (!this.data.isAdmin) {
      this.setData({ cmdMsg: '权限不足：仅 admin 可执行闸门控制' });
      return;
    }
    const o = opts || {};
    const silent = !!o.silent;
    const keepAutoOff = !!o.keepAutoOff;
    const isGateCmd = (cmd === 'gate_open' || cmd === 'gate_close' || cmd === 'gate_stop');
    this.setData({ cmdLoading: true, cmdMsg: '' });
    try {
      await api.requestJSON(withDev('/api/cmd'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        data: { cmd }
      });

      // Keep "锁定关闭自动" sticky across manual gate operations (firmware may clear latch on manual actions).
      if (keepAutoOff && isGateCmd) {
        try {
          await api.requestJSON(withDev('/api/cmd'), {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            data: { cmd: 'auto_latch_off' }
          });
        } catch (e2) {
          if (!silent) this.setData({ cmdMsg: '已执行闸门动作，但保持自动关闭失败（请再点一次）' });
        }
      }

      if (!silent) this.setData({ cmdMsg: `已发送：${CMD_LABELS[cmd] || cmd}` });
      await this.loadState();
    } catch (e) {
      if (!silent) this.setData({ cmdMsg: `发送失败：${e.message}` });
    } finally {
      this.setData({ cmdLoading: false });
    }
  },

  cmdGateOpen() { this.sendCmd('gate_open', { keepAutoOff: !!this._autoLatched }); },
  cmdGateClose() { this.sendCmd('gate_close', { keepAutoOff: !!this._autoLatched }); },
  cmdGateStop() { this.sendCmd('gate_stop', { keepAutoOff: !!this._autoLatched }); },
  cmdAutoOn() { this.sendCmd('auto_on'); },
  cmdAutoOff() { this.sendCmd('auto_off'); },
  cmdAutoLatchOff() { this.sendCmd('auto_latch_off'); },
  cmdAutoToggle() {
    if (this._autoLatched) {
      this.sendCmd('auto_on');
      return;
    }
    wx.showModal({
      title: '确认关闭自动',
      content: '将锁定关闭自动控制，是否继续？',
      success: (ret) => {
        if (!ret.confirm) return;
        this.sendCmd('auto_latch_off');
      }
    });
  },
  cmdManualEnd() { this.sendCmd('manual_end'); },

  async loadConfigSummary() {
    const empty = { mode: '--', tz: '--', dailyItems: [], cycleItems: [], leveldiffItems: [], msg: '' };
    try {
      let raw = '';
      const cacheUrl = `${withDev('/api/config')}${withDev('/api/config').includes('?') ? '&' : '?'}source=cache`;
      try {
        raw = await api.requestText(cacheUrl);
      } catch (e) {
        raw = await api.requestText(withDev('/api/config'));
      }
      const cfg = migrateConfig(fmt.safeParseJSON(raw, {}));
      const tzH = Math.round(num(cfg.tz_offset_ms, 28800000) / 3600000);
      const { dailyItems, cycleItems, leveldiffItems } = buildSummaryItems(cfg);
      this.setData({
        cfgSummary: {
          mode: modeLabel(cfg.mode || 'mixed'),
          tz: tzLabelFromHours(tzH),
          dailyItems,
          cycleItems,
          leveldiffItems,
          msg: ''
        }
      });
    } catch (e) {
      empty.msg = `读取失败：${e.message}`;
      this.setData({ cfgSummary: empty });
    }
  },

  async loadLog(force) {
    if (!force && !this._logVisible) return;
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
    if (!this.data.isAdmin) {
      this.setData({ logMsg: '权限不足：仅 admin 可清空日志' });
      return;
    }
    try {
      await api.requestJSON(withDev('/api/log/clear'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        data: { name: this.data.logTab }
      });
      this.setData({ logMsg: '已清空' });
      await this.loadLog(true);
    } catch (e) {
      this.setData({ logMsg: `清空失败：${e.message}` });
    }
  },

  async refreshAll() {
    await this.loadState();
    await this.loadConfigSummary();
    await this.loadLog(false);
  },

  refreshLog() {
    this.loadLog(true);
  },

  setLogTab(e) {
    const tab = (e.currentTarget.dataset.tab || 'error');
    this.setData({ logTab: tab });
    this.loadLog(true);
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
      const winInfo = (typeof wx.getWindowInfo === 'function') ? wx.getWindowInfo() : null;
      const devInfo = (typeof wx.getDeviceInfo === 'function') ? wx.getDeviceInfo() : null;
      const dpr = Number((winInfo && winInfo.pixelRatio) || (devInfo && devInfo.pixelRatio) || 1) || 1;
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
    this._gateTravelS = (Number.isFinite(this._gateTravelS) && this._gateTravelS > 0) ? this._gateTravelS : 10;
    if (!Number.isFinite(this._gateRatioCurrent)) this._gateRatioCurrent = 0;
    if (!Number.isFinite(this._gateRatioTarget)) this._gateRatioTarget = 0;
    this._schemAnimTimer = setInterval(() => {
      if (!this._schemCtx || !this._schemW || !this._schemH) return;
      const now = Date.now();
      const dt = this._lastAnimTs ? Math.max(0.016, (now - this._lastAnimTs) / 1000) : 0.016;
      this._lastAnimTs = now;

      const cur = Number.isFinite(this._gateRatioCurrent) ? this._gateRatioCurrent : 0;
      const target = Number.isFinite(this._gateRatioTarget) ? this._gateRatioTarget : cur;
      const travel = Math.max(1, Number.isFinite(this._gateTravelS) ? this._gateTravelS : 10);
      const step = dt / travel;
      const delta = target - cur;
      if (Math.abs(delta) > 0.0005 && dt > 0) {
        this._gateRatioCurrent = clamp(cur + Math.sign(delta) * Math.min(Math.abs(delta), step), 0, 1);
      } else {
        this._gateRatioCurrent = clamp(target, 0, 1);
      }

      this._flowPhase = (this._flowPhase + dt * 0.45) % 1;
      this.syncGateProgress(false);
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

    const inner = this._innerMm;
    const outer = this._outerMm;
    const delta = this._deltaMm;
    const gateState = Number(this._gateStateNum || 0);
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
    const gateW = 96;
    const leftW = (W - margin * 2 - gateW) / 2;
    const gateX = leftX + leftW;
    const rightX = gateX + gateW;
    const rightW = leftW;
    const slotX = gateX + 10;
    const slotW = gateW - 20;

    const frameStroke = 'rgba(226,232,240,0.9)';
    ctx.strokeStyle = frameStroke;
    ctx.lineWidth = 4;
    ctx.strokeRect(leftX, pondTop, leftW, pondH);
    ctx.strokeRect(rightX, pondTop, rightW, pondH);
    ctx.strokeRect(slotX, pondTop, slotW, pondH);

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
    const plateX = gateX + 20;
    const plateW = gateW - 40;
    const plateH = pondH + 16;
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
        ctx.fillRect(slotX, pondBottom - chanH, slotW, chanH);
      }

      const y = H - 58;
      const fromLeft = delta < 0;
      const ax = gateX + 12;
      const bx = gateX + gateW - 12;
      const color = Math.abs(delta) > 80 ? '#f59e0b' : '#22d3ee';
      ctx.strokeStyle = color;
      ctx.fillStyle = color;
      ctx.lineWidth = 3;
      ctx.beginPath();
      ctx.moveTo(ax, y);
      ctx.lineTo(bx, y);
      ctx.stroke();

      // Animated flow chevrons.
      const dir = fromLeft ? 1 : -1;
      const span = bx - ax;
      const phase = Number.isFinite(this._flowPhase) ? this._flowPhase : 0;
      for (let i = 0; i < 3; i += 1) {
        let p = ((i / 3) + phase) % 1;
        if (dir < 0) p = 1 - p;
        const x = ax + p * span;
        const s = 6;
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
    ctx.textAlign = 'center';
    ctx.fillText('水闸', gateX + gateW / 2, H - 20);
    ctx.textAlign = 'left';
  },

  goDeviceConfig() {
    if (!this.data.isAdmin) {
      wx.showToast({ title: '仅 admin 可进入规则页', icon: 'none' });
      return;
    }
    wx.navigateTo({ url: '/pages/device-config/index' });
  },

  goReplay() {
    wx.navigateTo({ url: '/pages/replay/index' });
  },

  goAdmin() {
    if (!this.data.isAdmin) {
      wx.showToast({ title: '仅 admin 可进入管理页', icon: 'none' });
      return;
    }
    wx.navigateTo({ url: '/pages/admin/index' });
  },

  async logout() {
    try { await api.requestJSON('/api/auth/logout', { method: 'POST' }); } catch (e) {}
    api.setCookieRaw('');
    getApp().globalData.user = null;
    try { wx.setStorageSync('skip_auto_login_once', true); } catch (e) {}
    wx.reLaunch({ url: '/pages/login/index' });
  }
});
