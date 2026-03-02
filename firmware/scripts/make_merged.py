Import("env")
import os
import re
import subprocess
from datetime import datetime


def _to_flash_freq(value):
    raw = str(value).strip().lower().replace("l", "")
    if raw.endswith("m"):
        return raw
    if raw.isdigit():
        hz = int(raw)
        if hz % 1000000 == 0:
            return f"{hz // 1000000}m"
    return raw


def _bootloader_offset(chip):
    # ESP32-S2/S3/C3/C6/H2 use 0x0; classic ESP32 uses 0x1000
    return "0x0" if chip in {"esp32s2", "esp32s3", "esp32c3", "esp32c6", "esp32h2"} else "0x1000"


def _sanitize_version(text):
    ver = str(text).strip()
    ver = re.sub(r"[^0-9A-Za-z._-]+", "_", ver)
    return ver or "unknown"


def _read_fw_version(project_dir, env):
    # Preferred: auto version injected by pre-script
    auto = env.get("AUTO_FW_VERSION") if env is not None else None
    if auto:
        return _sanitize_version(auto)

    # Fallback: parse from WS_Information.h
    info_h = os.path.join(project_dir, "src", "WS_Information.h")
    try:
        with open(info_h, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
    except OSError:
        return "unknown"

    m = re.search(r'^\s*#define\s+FW_VERSION\s+"([^"]+)"', content, flags=re.MULTILINE)
    if not m:
        return "unknown"

    return _sanitize_version(m.group(1))


def make_merged(source=None, target=None, env=None, **kwargs):
    if env is None:
        env = kwargs.get("env")
    if env is None:
        raise RuntimeError("SCons env is required")

    build_dir = env.subst("$BUILD_DIR")
    proj_dir = env.subst("$PROJECT_DIR")
    dist_dir = os.path.join(proj_dir, "dist")
    os.makedirs(dist_dir, exist_ok=True)

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    boot_app0 = os.path.join(build_dir, "boot_app0.bin")
    app = os.path.join(build_dir, "firmware.bin")

    fw_version = _read_fw_version(proj_dir, env)
    date_str = datetime.now().strftime("%Y%m%d")
    named_merged = os.path.join(dist_dir, f"FISH-{fw_version}-{date_str}.bin")

    esptool_pkg = env.PioPlatform().get_package_dir("tool-esptoolpy")
    esptool_py = os.path.join(esptool_pkg, "esptool.py")

    chip = str(env.BoardConfig().get("build.mcu", "esp32")).lower()
    flash_mode = env.BoardConfig().get("build.flash_mode", "dio")
    flash_freq = _to_flash_freq(env.BoardConfig().get("build.f_flash", "40000000L"))
    flash_size = env.BoardConfig().get("upload.flash_size", "4MB")

    cmd = [
        env.subst("$PYTHONEXE"), esptool_py,
        "--chip", chip,
        "merge_bin",
        "-o", named_merged,
        "--flash_mode", str(flash_mode),
        "--flash_freq", str(flash_freq),
        "--flash_size", str(flash_size),
        _bootloader_offset(chip), bootloader,
        "0x8000", partitions,
    ]

    if os.path.exists(boot_app0):
        cmd += ["0xe000", boot_app0]

    cmd += ["0x10000", app]

    print("[merged] " + " ".join(cmd))
    subprocess.check_call(cmd)
    print(f"[merged] wrote {named_merged}")


env.AddPostAction("buildprog", make_merged)
