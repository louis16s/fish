Import("env")

import sys


def _force_single_job_on_windows():
    if not sys.platform.startswith("win"):
        return
    try:
        from SCons.Script import GetOption, SetOption  # type: ignore
    except Exception:
        print("[jobs] warning: SCons Script API not available; cannot force -j1")
        return

    try:
        cur = None
        try:
            cur = GetOption("num_jobs")
        except Exception:
            cur = None

        # On Windows, parallel builds sometimes race/lock .o files and "ar" reports
        # "No such file or directory" for an object that is being written/scanned.
        if cur != 1:
            SetOption("num_jobs", 1)
            print("[jobs] windows: forced SCons num_jobs=1 to avoid intermittent ar missing .o")
    except Exception as e:
        print("[jobs] warning: failed to force SCons num_jobs=1:", e)


_force_single_job_on_windows()

