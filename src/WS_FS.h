#ifndef _WS_FS_H_
#define _WS_FS_H_

#include <LittleFS.h>

// Single place to mount LittleFS. Safe to call multiple times.
bool WS_FS_EnsureMounted();

#endif

