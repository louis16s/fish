App({
  globalData: {
    user: null,
    currentDeviceId: '',
    devices: []
  },
  onLaunch() {
    try {
      const did = wx.getStorageSync('current_device_id');
      if (did) this.globalData.currentDeviceId = String(did);
    } catch (e) {}
  }
});
