const api = require('../../utils/api');
const fmt = require('../../utils/format');

function denyAndBack(message) {
  wx.showToast({ title: message, icon: 'none' });
  setTimeout(() => {
    wx.reLaunch({ url: '/pages/panel/index' });
  }, 500);
}

Page({
  data: {
    who: '--',
    mqttText: '--',
    mqttTagClass: '',
    dbText: '--',
    dbTagClass: '',
    retentionDays: 30,
    retentionMsg: '',
    users: [],
    devices: [],
    deviceMsg: '',
    userMsg: '',
    createUserCollapsed: true,
    newUser: '',
    newPass: '',
    roleOptions: ['user', 'admin'],
    roleIndex: 0
  },

  onShow() {
    this.bootstrap();
  },

  async bootstrap() {
    try {
      const me = await api.requestJSON('/api/auth/me');
      const user = me.user || {};
      this.setData({ who: `用户 ${user.username || '--'} | ${user.role || '--'}` });
      if (String(user.role || '').toLowerCase() !== 'admin') {
        denyAndBack('仅 admin 可进入管理页');
        return;
      }
    } catch (e) {
      wx.reLaunch({ url: '/pages/login/index' });
      return;
    }
    await this.reloadAll();
  },

  async reloadAll() {
    await Promise.all([
      this.loadStatus(),
      this.loadRetention(),
      this.loadUsers(),
      this.loadDevices()
    ]);
  },

  async loadStatus() {
    try {
      const j = await api.requestJSON('/healthz');
      this.setData({
        mqttText: j.mqtt ? '已连接' : '未连接',
        mqttTagClass: j.mqtt ? 'tag-good' : 'tag-bad',
        dbText: j.db ? 'OK' : 'ERR',
        dbTagClass: j.db ? 'tag-good' : 'tag-bad'
      });
    } catch (e) {
      this.setData({ mqttText: '失败', mqttTagClass: 'tag-bad', dbText: '失败', dbTagClass: 'tag-bad' });
    }
  },

  async loadRetention() {
    try {
      const j = await api.requestJSON('/api/admin/settings');
      this.setData({ retentionDays: Number(j.retention_days || 30), retentionMsg: '' });
    } catch (e) {
      this.setData({ retentionMsg: `加载失败：${e.message}` });
    }
  },

  onRetentionInput(e) {
    this.setData({ retentionDays: Number(e.detail.value || 30) });
  },

  async saveRetention() {
    try {
      const j = await api.requestJSON('/api/admin/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        data: { retention_days: Number(this.data.retentionDays || 30) }
      });
      this.setData({ retentionMsg: `已保存：${j.retention_days}` });
    } catch (e) {
      this.setData({ retentionMsg: `保存失败：${e.message}` });
    }
  },

  async loadUsers() {
    try {
      const j = await api.requestJSON('/api/admin/users');
      this.setData({ users: Array.isArray(j.users) ? j.users : [] });
    } catch (e) {
      this.setData({ users: [] });
    }
  },

  async loadDevices() {
    try {
      let list = [];
      let deviceMsg = '';
      try {
        const j = await api.requestJSON('/api/admin/devices/overview');
        list = Array.isArray(j.devices) ? j.devices : [];
      } catch (e) {
        const fallback = await api.requestJSON('/api/devices');
        list = Array.isArray(fallback.devices) ? fallback.devices : [];
        deviceMsg = `设备概览接口不可用，已回退基础列表：${e.message}`;
      }

      const mapped = list.map((d) => ({
        device_id: d.device_id || '',
        last_seen_at: fmt.fmtDateTime(d.last_seen_at),
        fw_version: d.fw_version || '--',
        wifi_rssi: Number.isFinite(Number(d.wifi_rssi)) ? `${Number(d.wifi_rssi)} dBm` : '--',
        lan_ip: d.lan_ip || '--'
      }));
      this.setData({ devices: mapped, deviceMsg });
    } catch (e) {
      this.setData({ devices: [], deviceMsg: `设备列表加载失败：${e.message}` });
    }
  },

  toggleCreateUserFold() {
    this.setData({ createUserCollapsed: !this.data.createUserCollapsed });
  },

  onNewUser(e) { this.setData({ newUser: e.detail.value || '' }); },
  onNewPass(e) { this.setData({ newPass: e.detail.value || '' }); },
  onRoleChange(e) { this.setData({ roleIndex: Number(e.detail.value) || 0 }); },

  async createUser() {
    const username = (this.data.newUser || '').trim();
    const password = this.data.newPass || '';
    const role = this.data.roleOptions[this.data.roleIndex] || 'user';
    if (!username || password.length < 8) {
      this.setData({ userMsg: '用户名不能为空，密码至少8位' });
      return;
    }
    try {
      await api.requestJSON('/api/admin/users', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        data: { username, password, role }
      });
      this.setData({ userMsg: '已创建', newPass: '' });
      await this.loadUsers();
    } catch (e) {
      this.setData({ userMsg: `创建失败：${e.message}` });
    }
  },

  async toggleDisable(e) {
    const id = Number(e.currentTarget.dataset.id || 0);
    const targetDisabled = !!e.currentTarget.dataset.targetDisabled;
    const role = String(e.currentTarget.dataset.role || '');
    const username = String(e.currentTarget.dataset.username || '').trim().toLowerCase();
    if (!id) return;
    if (role === 'admin' || username === 'admin') {
      this.setData({ userMsg: 'admin 账号不可禁用' });
      return;
    }
    const actionText = targetDisabled ? '禁用' : '启用';
    wx.showModal({
      title: `${actionText}账号`,
      content: `确认${actionText}该账号？`,
      success: async (ret) => {
        if (!ret.confirm) return;
        try {
          await api.requestJSON(`/api/admin/users/${id}/disable`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            data: { disabled: targetDisabled }
          });
          this.setData({ userMsg: `已${actionText}` });
          await this.loadUsers();
        } catch (err) {
          this.setData({ userMsg: `操作失败：${err.message}` });
        }
      }
    });
  },

  resetPassword(e) {
    const id = Number(e.currentTarget.dataset.id || 0);
    if (!id) return;
    wx.showModal({
      title: '重置密码',
      editable: true,
      placeholderText: '请输入新密码（>=8）',
      success: async (res) => {
        if (!res.confirm) return;
        const password = (res.content || '').trim();
        if (password.length < 8) {
          this.setData({ userMsg: '密码至少8位' });
          return;
        }
        try {
          await api.requestJSON(`/api/admin/users/${id}/password`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            data: { password }
          });
          this.setData({ userMsg: '密码已重置' });
        } catch (err) {
          this.setData({ userMsg: `重置失败：${err.message}` });
        }
      }
    });
  },

  backPanel() {
    wx.navigateBack({ delta: 1 });
  }
});
