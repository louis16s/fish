const api = require('../../utils/api');

Page({
  onLoad() {
    this.enter();
  },

  async enter() {
    try {
      await api.requestJSON('/api/auth/me');
      wx.reLaunch({ url: '/pages/panel/index' });
    } catch (e) {
      wx.reLaunch({ url: '/pages/login/index' });
    }
  }
});
