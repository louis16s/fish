const api = require('../../utils/api');

Page({
  data: {
    username: '',
    password: '',
    rememberPassword: true,
    loading: false,
    msg: '',
    baseUrl: api.BASE_URL
  },

  onLoad() {
    try {
      const remember = wx.getStorageSync('remember_password') !== false;
      const username = wx.getStorageSync('remember_username') || '';
      const password = remember ? (wx.getStorageSync('remember_password_value') || '') : '';
      this.setData({
        rememberPassword: remember,
        username: String(username),
        password: String(password)
      });
      if (remember && username && password) {
        this.onLogin(true);
      }
    } catch (e) {}
  },

  onUserInput(e) {
    this.setData({ username: e.detail.value || '' });
  },

  onPassInput(e) {
    this.setData({ password: e.detail.value || '' });
  },

  onRememberChange(e) {
    const checked = !!(e && e.detail && e.detail.value);
    this.setData({ rememberPassword: checked });
  },

  async onLogin(silent) {
    if (silent && typeof silent === 'object') silent = false;
    const username = (this.data.username || '').trim();
    const password = this.data.password || '';
    if (!username || !password) {
      if (!silent) this.setData({ msg: '请输入用户名和密码' });
      return;
    }

    this.setData({ loading: true, msg: silent ? this.data.msg : '' });
    try {
      await api.requestJSON('/api/auth/login', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        data: { username, password },
        useCookie: true
      });
      const me = await api.requestJSON('/api/auth/me');
      const app = getApp();
      app.globalData.user = me.user || null;

      try {
        wx.setStorageSync('remember_password', !!this.data.rememberPassword);
        wx.setStorageSync('remember_username', username);
        if (this.data.rememberPassword) {
          wx.setStorageSync('remember_password_value', password);
        } else {
          wx.removeStorageSync('remember_password_value');
        }
      } catch (e) {}

      wx.reLaunch({ url: '/pages/panel/index' });
    } catch (e) {
      if (!silent) this.setData({ msg: `登录失败：${e.message || '网络错误'}` });
    } finally {
      this.setData({ loading: false });
    }
  }
});
