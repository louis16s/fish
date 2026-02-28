const api = require('../../utils/api');

Page({
  data: {
    username: '',
    password: '',
    loading: false,
    msg: '',
    baseUrl: api.BASE_URL
  },

  onUserInput(e) {
    this.setData({ username: e.detail.value || '' });
  },

  onPassInput(e) {
    this.setData({ password: e.detail.value || '' });
  },

  async onLogin() {
    const username = (this.data.username || '').trim();
    const password = this.data.password || '';
    if (!username || !password) {
      this.setData({ msg: '请输入用户名和密码' });
      return;
    }

    this.setData({ loading: true, msg: '' });
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
      wx.reLaunch({ url: '/pages/panel/index' });
    } catch (e) {
      this.setData({ msg: `登录失败：${e.message || '网络错误'}` });
    } finally {
      this.setData({ loading: false });
    }
  }
});
