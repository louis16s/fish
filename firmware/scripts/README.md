# Firmware Build Scripts

本目录存放 PlatformIO `extra_scripts`，用于自动化固件构建流程。

## 1. 脚本职责

1. `auto_version.py`
- 预构建脚本。
- 生成 `firmware/src/auto_fw_version.h`，写入当前 `FW_VERSION`。

2. `embed_ui_assets.py`
- 预构建脚本。
- 将 `firmware/data/ui/*` 资源打包为 C 数组，输出到 `src/WS_UI_Assets.*`，用于 LittleFS 缺失时兜底。

3. `make_merged.py`
- 后构建脚本。
- 合并 bootloader/partitions/app，产出单文件固件到 `firmware/dist/`。

## 2. 触发方式

这些脚本由 `firmware/platformio.ini` 的 `extra_scripts` 自动触发，常用命令：

```bash
cd firmware
pio run
pio run -t upload
pio run -t uploadfs
```

## 3. 常见问题

1. 页面资源更新后设备端未生效
- 先执行 `pio run -t uploadfs`。
- 若仍不生效，检查 `embed_ui_assets.py` 是否在 `extra_scripts` 中启用。

2. 合并固件未生成
- 检查 `pio run` 是否成功完成（后构建脚本只在成功后执行）。
- 检查 `dist/` 目录写权限。

3. 版本号未刷新
- 确认 `auto_version.py` 已在预构建阶段执行（查看构建日志）。
