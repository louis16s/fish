# Scripts

Build helper scripts for PlatformIO.

- `auto_version.py`: pre-build script, writes `src/auto_fw_version.h` with `FW_VERSION`.
- `make_merged.py`: post-build script, merges bootloader/partitions/app into a single flashable `.bin` under `dist/`.

These scripts are executed by `platformio.ini` via `extra_scripts`.

