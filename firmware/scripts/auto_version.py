Import("env")

from datetime import datetime
import os
import re
import time


def _sanitize(ver: str) -> str:
    return re.sub(r"[^0-9A-Za-z._-]+", "_", ver)


now = datetime.now()
ms = int(time.time_ns() // 1_000_000) % 1000
version = _sanitize(f"v{now:%Y.%m.%d-%H%M%S}-{ms:03d}")

# expose for other scripts (e.g. make_merged.py)
env["AUTO_FW_VERSION"] = version

# Write generated version header to avoid command-line macro quoting issues.
project_dir = env.subst("$PROJECT_DIR")
out_h = os.path.join(project_dir, "src", "auto_fw_version.h")
content = (
    "#ifndef _AUTO_FW_VERSION_H_\n"
    "#define _AUTO_FW_VERSION_H_\n"
    f"#define FW_VERSION \"{version}\"\n"
    "#endif\n"
)

with open(out_h, "w", encoding="utf-8", newline="\n") as f:
    f.write(content)

print(f"[auto-version] FW_VERSION={version}")
print(f"[auto-version] wrote {out_h}")
