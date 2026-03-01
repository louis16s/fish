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

function calcYRange(points) {
  const vals = [];
  (points || []).forEach((pt) => {
    if (pt && pt.inner_ok && Number.isFinite(Number(pt.inner_mm))) vals.push(Number(pt.inner_mm));
    if (pt && pt.outer_ok && Number.isFinite(Number(pt.outer_mm))) vals.push(Number(pt.outer_mm));
  });
  if (!vals.length) return { yMin: 0, yMax: 5000 };

  let minV = Math.min(...vals);
  let maxV = Math.max(...vals);
  if (!Number.isFinite(minV) || !Number.isFinite(maxV)) return { yMin: 0, yMax: 5000 };

  if (minV === maxV) {
    const pad = Math.max(50, Math.abs(minV) * 0.1);
    minV -= pad;
    maxV += pad;
  }
  const span = Math.max(1, maxV - minV);
  const pad = Math.max(20, span * 0.08);
  return {
    yMin: Math.floor(minV - pad),
    yMax: Math.ceil(maxV + pad)
  };
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
    historyMeta: '',
    historyPointText: '',
    historyPointTs: '',
    historyPointDetail: '',
    rangeHint: '每条记录代表一次设备遥测快照（telemetry payload）。单位：mm。',
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
    await this.loadData();
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
          idx: 0,
          ts: fmt.fmtDateTime(r.ts),
          inner: inner == null ? '--' : `${inner}mm`,
          outer: outer == null ? '--' : `${outer}mm`,
          delta: delta == null ? '--' : `${delta}mm`,
          gate: fmt.gateStateText(t.gate_state),
          auto,
          alarm: fmt.alarmText(t.alarm)
        };
      });
      list.forEach((x, i) => { x.idx = i + 1; });
      this.setData({
        rows: list,
        rawRows: rows,
        meta: `返回 ${rows.length} 条，范围 ${fmt.fmtDateTime(from)} ~ ${fmt.fmtDateTime(to)}`,
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
      this.setData({ historyPointText: '', historyPointTs: '', historyPointDetail: '' });
      this.drawHistory(pts);
    } catch (e) {
      this.setData({ msg: `历史加载失败：${e.message}` });
    }
  },

  drawHistory(points) {
    const query = wx.createSelectorQuery().in(this);
    query.select('#histCanvas').fields({ node: true, size: true, rect: true }).exec((res) => {
      if (!res || !res[0] || !res[0].node) return;
      const canvas = res[0].node;
      const width = res[0].width;
      const height = res[0].height;
      this._histCanvasLeft = Number(res[0].left || 0);
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
        this.setData({ historyMeta: '' });
        this._histPointsForTap = [];
        return;
      }

      const pointsSorted = points.slice().sort((a, b) => Number(a.ts_s || 0) - Number(b.ts_s || 0));

      const { yMin, yMax } = calcYRange(pointsSorted);
      const tsVals = pointsSorted
        .map((pt) => Number(pt.ts_s))
        .filter((v) => Number.isFinite(v));
      const xMin = tsVals.length ? Math.min(...tsVals) : Number(pointsSorted[0].ts_s || 0);
      const xMaxRaw = tsVals.length ? Math.max(...tsVals) : Number(pointsSorted[pointsSorted.length - 1].ts_s || xMin + 1);
      const xMax = xMaxRaw > xMin ? xMaxRaw : (xMin + 1);

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
      ctx.strokeStyle = 'rgba(255,255,255,.22)';
      ctx.beginPath();
      ctx.moveTo(p.l, p.t);
      ctx.lineTo(p.l, height - p.b);
      ctx.lineTo(width - p.r, height - p.b);
      ctx.stroke();

      ctx.fillStyle = 'rgba(165,180,207,.92)';
      ctx.font = '11px sans-serif';
      for (let i = 0; i <= 4; i += 1) {
        const y = p.t + (ph * i) / 4;
        const v = yMax - ((y - p.t) / (ph || 1)) * (yMax - yMin);
        ctx.fillText(`${Math.round(v)}mm`, 6, y + 4);
      }
      const t0 = new Date((xMin || 0) * 1000);
      const t1 = new Date((xMax || 0) * 1000);
      const tShort = (d) => `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`;
      ctx.fillText(tShort(t0), p.l, height - 6);
      const endTxt = tShort(t1);
      const w = ctx.measureText(endTxt).width;
      ctx.fillText(endTxt, width - p.r - w, height - 6);
      ctx.fillText('时间', Math.max(p.l + 6, width / 2 - 14), height - 6);

      const drawLine = (selector, color) => {
        let started = false;
        ctx.beginPath();
        pointsSorted.forEach((pt) => {
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

      this._histPointsForTap = pointsSorted.map((pt) => {
        const ts = Number(pt.ts_s || 0);
        return {
          x: mapX(ts),
          ts,
          inner: (pt.inner_ok && Number.isFinite(Number(pt.inner_mm))) ? Number(pt.inner_mm) : null,
          outer: (pt.outer_ok && Number.isFinite(Number(pt.outer_mm))) ? Number(pt.outer_mm) : null
        };
      });

      this.setData({
        historyMeta: ''
      });
    });
  },

  buildCsvText() {
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
    return lines.join('\n');
  },

  async exportData() {
    const rows = this.data.rawRows || [];
    if (!rows.length) {
      this.setData({ msg: '暂无可导出的范围结果，请先查询' });
      return;
    }

    wx.showActionSheet({
      itemList: ['导出 JSON', '导出 CSV'],
      success: (ret) => {
        const idx = Number(ret.tapIndex || 0);
        const isJson = idx === 0;
        const ext = isJson ? 'json' : 'csv';
        const ts = Date.now();
        const fileName = `replay_${ts}.${ext}`;
        const content = isJson ? JSON.stringify(rows, null, 2) : this.buildCsvText();
        const filePath = `${wx.env.USER_DATA_PATH}/${fileName}`;
        const fs = wx.getFileSystemManager();
        fs.writeFile({
          filePath,
          data: content,
          encoding: 'utf8',
          success: () => {
            if (typeof wx.shareFileMessage === 'function') {
              wx.shareFileMessage({
                filePath,
                fileName,
                success: () => this.setData({ msg: `已导出并可发送：${fileName}` }),
                fail: () => this.setData({ msg: `文件已生成：${fileName}（当前环境不支持直接发送）` })
              });
              return;
            }
            this.setData({ msg: `文件已生成：${fileName}（当前环境不支持直接发送）` });
          },
          fail: (e) => this.setData({ msg: `导出失败：${(e && e.errMsg) || 'write_failed'}` })
        });
      }
    });
  },

  onHistTap(e) {
    const arr = this._histPointsForTap || [];
    if (!arr.length) return;
    let clientX = 0;
    if (e && e.detail && Number.isFinite(Number(e.detail.x))) {
      clientX = Number(e.detail.x);
    } else if (e && e.touches && e.touches[0] && Number.isFinite(Number(e.touches[0].clientX))) {
      clientX = Number(e.touches[0].clientX);
    } else if (e && e.touches && e.touches[0] && Number.isFinite(Number(e.touches[0].x))) {
      clientX = Number(e.touches[0].x);
    }
    const x = clientX - (Number(this._histCanvasLeft || 0));
    let best = arr[0];
    let bestDx = Math.abs((best && best.x) - x);
    arr.forEach((p) => {
      const dx = Math.abs(p.x - x);
      if (dx < bestDx) {
        best = p;
        bestDx = dx;
      }
    });
    if (!best) return;
    const tsText = fmt.fmtDateTime(new Date(best.ts * 1000).toISOString());
    const inner = best.inner == null ? '--' : `${Math.round(best.inner)}mm`;
    const outer = best.outer == null ? '--' : `${Math.round(best.outer)}mm`;
    const delta = (best.inner != null && best.outer != null) ? `${Math.round(best.inner - best.outer)}mm` : '--';
    this.setData({
      historyPointText: `${tsText} | 内塘 ${inner} | 外塘 ${outer} | Δ ${delta}`,
      historyPointTs: tsText,
      historyPointDetail: `内塘 ${inner} | 外塘 ${outer} | Δ ${delta}`
    });
  },

  backPanel() {
    wx.navigateBack({ delta: 1 });
  }
});
