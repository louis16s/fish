const api = require('../../utils/api');
const SKIP_AUTO_LOGIN_ONCE_KEY = 'skip_auto_login_once';

Page({
  data: {
    username: '',
    password: '',
    rememberPassword: true,
    showPassword: false,
    loading: false,
    msg: '',
    baseUrl: api.BASE_URL
  },

  onLoad() {
    this.bootstrap();
  },

  async bootstrap() {
    const hasSession = await this.tryResumeSession();
    if (hasSession) return;

    try {
      const skipAutoLogin = !!wx.getStorageSync(SKIP_AUTO_LOGIN_ONCE_KEY);
      if (skipAutoLogin) wx.removeStorageSync(SKIP_AUTO_LOGIN_ONCE_KEY);
      const remember = wx.getStorageSync('remember_password') !== false;
      const username = wx.getStorageSync('remember_username') || '';
      const password = remember ? (wx.getStorageSync('remember_password_value') || '') : '';
      this.setData({
        rememberPassword: remember,
        username: String(username),
        password: String(password)
      });
      if (!skipAutoLogin && remember && username && password) {
        await this.onLogin(true);
      }
    } catch (e) {}
  },

  async tryResumeSession() {
    try {
      const me = await api.requestJSON('/api/auth/me');
      const app = getApp();
      app.globalData.user = me.user || null;
      wx.reLaunch({ url: '/pages/panel/index' });
      return true;
    } catch (e) {
      return false;
    }
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

  onShowPasswordChange(e) {
    const checked = !!(e && e.detail && e.detail.value);
    this.setData({ showPassword: checked });
  },

  async onLogin(silent) {
    if (silent && typeof silent === 'object') silent = false;
    if (this.data.loading) return;

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
        wx.removeStorageSync(SKIP_AUTO_LOGIN_ONCE_KEY);
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
      api.setCookieRaw('');
      this.setData({ msg: silent ? '自动登录失败，请重新输入密码' : `登录失败：${e.message || '网络错误'}` });
    } finally {
      this.setData({ loading: false });
    }
  }
});
