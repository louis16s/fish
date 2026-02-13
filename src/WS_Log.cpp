#include "WS_Log.h"
#include "WS_FS.h"

#include <LittleFS.h>
#include <stdarg.h>
#include <stdio.h>

static uint32_t (*g_nowEpoch)() = nullptr;

static const char* kErrorPath = "/log_error.txt";
static const char* kMeasurePath = "/log_measure.txt";
static const char* kActionPath = "/log_action.txt";
static const size_t kMaxBytes = 256U * 1024U;

static void RotateIfNeeded(const char* path)
{
  if (!WS_FS_EnsureMounted()) {
    return;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    return;
  }
  const size_t sz = (size_t)f.size();
  f.close();
  if (sz < kMaxBytes) {
    return;
  }

  String bak = String(path) + ".1";
  if (LittleFS.exists(bak)) {
    LittleFS.remove(bak);
  }
  LittleFS.rename(path, bak);
}

static void AppendLine(const char* path, const char* tag, const char* fmt, va_list ap)
{
  if (!WS_FS_EnsureMounted()) {
    return;
  }

  RotateIfNeeded(path);

  char msg[256];
  vsnprintf(msg, sizeof(msg), fmt, ap);

  char line[320];
  if (g_nowEpoch) {
    const uint32_t ts = g_nowEpoch();
    snprintf(line, sizeof(line), "%lu [%s] %s\r\n", (unsigned long)ts, tag, msg);
  } else {
    snprintf(line, sizeof(line), "ms=%lu [%s] %s\r\n", (unsigned long)millis(), tag, msg);
  }

  File f = LittleFS.open(path, "a");
  if (!f) {
    return;
  }
  f.print(line);
  f.close();
}

void WS_Log_SetTimeProvider(uint32_t (*nowEpoch)())
{
  g_nowEpoch = nowEpoch;
}

void WS_Log_Init()
{
  (void)WS_FS_EnsureMounted();
}

void WS_Log_Error(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  AppendLine(kErrorPath, "ERR", fmt, ap);
  va_end(ap);
}

void WS_Log_Measure(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  AppendLine(kMeasurePath, "MEAS", fmt, ap);
  va_end(ap);
}

void WS_Log_Action(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  AppendLine(kActionPath, "ACT", fmt, ap);
  va_end(ap);
}

