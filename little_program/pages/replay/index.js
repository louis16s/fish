const api = require('../../utils/api');
const fmt = require('../../utils/format');

function pickDeviceId() {
  return getApp().globalData.currentDeviceId || '';
}

function withDev(path) {
  const q = api.buildQuery({ device_id: pickDeviceId() });
  return `${path}${q}`;
}

function toIso(dt) {
  try {
    const d = new Date(dt);
    if (!Number.isFinite(d.getTime())) return '';
    return d.toISOString();
  } catch (e) {
    return '';
  }
}

function csvEsc(s) {
  const t = s == null ? '' : String(s);
  if (/[",\n]/.test(t)) return `"${t.replace(/"/g, '""')}"`;
  return t;
}

Page({
  data: {
    deviceOptions: [],
    deviceIndex: 0,
    currentDeviceLabel: '--',
    from: '',
    to: '',
    limit: 2000,
    loading: false,
    meta: '--',
    msg: '',
    rows: [],
    rawRows: []
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

    if (!this.data.from || !this.data.to) {
      const now = new Date();
      const from = new Date(now.getTime() - 3600 * 1000);
      this.setData({ from: from.toISOString(), to: now.toISOString() });
    }
    await this.loadHistoryWindow(3600);
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

  onFromInput(e) { this.setData({ from: e.detail.value || '' }); },
  onToInput(e) { this.setData({ to: e.detail.value || '' }); },
  onLimitInput(e) { this.setData({ limit: Number(e.detail.value || 2000) }); },

  onDeviceChange(e) {
    const idx = Number(e.detail.value) || 0;
    const item = this.data.deviceOptions[idx];
    const did = item ? item.value : '';
    getApp().globalData.currentDeviceId = did;
    wx.setStorageSync('current_device_id', did || '');
    this.setData({ deviceIndex: idx, currentDeviceLabel: item ? item.label : '--' });
    this.loadHistoryWindow(3600);
  },

  async loadData() {
    const from = toIso(this.data.from);
    const to = toIso(this.data.to);
    const limit = Math.max(50, Math.min(20000, Number(this.data.limit || 2000)));
    if (!from || !to) {
      this.setData({ msg: '时间格式无效，请使用 ISO 时间字符串' });
      return;
    }

    this.setData({ loading: true, msg: '' });
    try {
      const qs = api.buildQuery({ device_id: pickDeviceId(), from, to, limit });
      const j = await api.requestJSON(`/api/telemetry/range${qs}`);
      const rows = Array.isArray(j.rows) ? j.rows : [];
      const list = rows.map((r) => {
        const t = r.payload || {};
        const s1 = t.sensor1 || {};
        const s2 = t.sensor2 || {};
        const inner = s1.valid ? Number(s1.mm) : null;
        const outer = s2.valid ? Number(s2.mm) : null;
        const delta = (inner != null && outer != null) ? (inner - outer) : null;
        const auto = t.auto_latched ? '锁定关' : (t.auto_gate ? '开' : '关');
        return {
          ts: fmt.fmtDateTime(r.ts),
          inner: inner == null ? '--' : `${inner}mm`,
          outer: outer == null ? '--' : `${outer}mm`,
          delta: delta == null ? '--' : `${delta}mm`,
          gate: fmt.gateStateText(t.gate_state),
          auto,
          alarm: fmt.alarmText(t.alarm)
        };
      });
      this.setData({
        rows: list,
        rawRows: rows,
        meta: `返回 ${rows.length} 条`,
        msg: ''
      });
    } catch (e) {
      this.setData({ msg: `查询失败：${e.message}` });
    } finally {
      this.setData({ loading: false });
    }
  },

  async loadHistory(e) {
    const win = Number(e.currentTarget.dataset.win || 3600);
    await this.loadHistoryWindow(win);
  },

  async loadHistoryWindow(windowS) {
    try {
      const qs = api.buildQuery({ device_id: pickDeviceId(), window_s: windowS, max_points: 700 });
      const j = await api.requestJSON(`/api/history${qs}`);
      const pts = Array.isArray(j.points) ? j.points : [];
      this.drawHistory(pts);
    } catch (e) {
      this.setData({ msg: `历史加载失败：${e.message}` });
    }
  },

  drawHistory(points) {
    const query = wx.createSelectorQuery().in(this);
    query.select('#histCanvas').fields({ node: true, size: true }).exec((res) => {
      if (!res || !res[0] || !res[0].node) return;
      const canvas = res[0].node;
      const width = res[0].width;
      const height = res[0].height;
      const dpr = wx.getSystemInfoSync().pixelRatio || 1;
      canvas.width = width * dpr;
      canvas.height = height * dpr;
      const ctx = canvas.getContext('2d');
      ctx.scale(dpr, dpr);
      ctx.clearRect(0, 0, width, height);

      ctx.fillStyle = 'rgba(2,6,23,.45)';
      ctx.fillRect(0, 0, width, height);

      if (!points.length) {
        ctx.fillStyle = '#94a3b8';
        ctx.font = '14px sans-serif';
        ctx.fillText('暂无历史数据', 20, 28);
        return;
      }

      const values = [];
      points.forEach((p) => {
        if (p.inner_ok && Number.isFinite(p.inner_mm)) values.push(Number(p.inner_mm));
        if (p.outer_ok && Number.isFinite(p.outer_mm)) values.push(Number(p.outer_mm));
      });
      const minV = Math.min.apply(null, values.length ? values : [0]);
      const maxV = Math.max.apply(null, values.length ? values : [100]);
      const pad = Math.max(30, Math.round((maxV - minV || 50) * 0.2));
      const yMin = minV - pad;
      const yMax = maxV + pad;
      const xMin = Number(points[0].ts_s || 0);
      const xMax = Number(points[points.length - 1].ts_s || xMin + 1);

      const p = { l: 54, r: 16, t: 14, b: 28 };
      const pw = width - p.l - p.r;
      const ph = height - p.t - p.b;
      const mapX = (x) => p.l + ((x - xMin) / ((xMax - xMin) || 1)) * pw;
      const mapY = (y) => p.t + ph - ((y - yMin) / ((yMax - yMin) || 1)) * ph;

      ctx.strokeStyle = 'rgba(255,255,255,.08)';
      for (let i = 0; i <= 4; i += 1) {
        const y = p.t + (ph * i) / 4;
        ctx.beginPath();
        ctx.moveTo(p.l, y);
        ctx.lineTo(width - p.r, y);
        ctx.stroke();
      }

      const drawLine = (selector, color) => {
        let started = false;
        ctx.beginPath();
        points.forEach((pt) => {
          const v = selector(pt);
          if (v == null) {
            started = false;
            return;
          }
          const x = mapX(Number(pt.ts_s || 0));
          const y = mapY(v);
          if (!started) {
            ctx.moveTo(x, y);
            started = true;
          } else {
            ctx.lineTo(x, y);
          }
        });
        ctx.strokeStyle = color;
        ctx.lineWidth = 2;
        ctx.stroke();
      };

      drawLine((pt) => (pt.inner_ok ? Number(pt.inner_mm) : null), '#4ade80');
      drawLine((pt) => (pt.outer_ok ? Number(pt.outer_mm) : null), '#38bdf8');
    });
  },

  copyJson() {
    const text = JSON.stringify(this.data.rawRows || [], null, 2);
    wx.setClipboardData({ data: text });
  },

  copyCsv() {
    const rows = this.data.rawRows || [];
    const head = ['ts', 'inner_mm', 'outer_mm', 'delta_mm', 'gate_state', 'auto_gate', 'auto_latched', 'alarm_active', 'alarm_severity', 'alarm_text'];
    const lines = [head.join(',')];
    rows.forEach((r) => {
      const t = r.payload || {};
      const s1 = t.sensor1 || {};
      const s2 = t.sensor2 || {};
      const inner = s1.valid ? Number(s1.mm) : '';
      const outer = s2.valid ? Number(s2.mm) : '';
      const delta = (inner !== '' && outer !== '') ? (inner - outer) : '';
      const a = t.alarm || {};
      const line = [
        r.ts || '',
        inner,
        outer,
        delta,
        t.gate_state == null ? '' : t.gate_state,
        t.auto_gate ? 1 : 0,
        t.auto_latched ? 1 : 0,
        a.active ? 1 : 0,
        a.severity == null ? '' : a.severity,
        a.text || ''
      ].map(csvEsc).join(',');
      lines.push(line);
    });
    wx.setClipboardData({ data: lines.join('\n') });
  },

  backPanel() {
    wx.navigateBack({ delta: 1 });
  }
});
