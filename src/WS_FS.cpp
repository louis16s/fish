#include "WS_FS.h"

bool WS_FS_EnsureMounted()
{
  static bool tried = false;
  static bool ok = false;
  if (!tried) {
    tried = true;
    ok = LittleFS.begin(false);
  }
  return ok;
}

