#include "WS_Log.h"
#include "WS_FS.h"

#include <LittleFS.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static uint32_t (*g_nowEpoch)() = nullptr;
static WS_LogLineSink g_sink = nullptr;

static const char* kErrorPath = "/log_error.txt";
static const char* kMeasurePath = "/log_measure.txt";
static const char* kActionPath = "/log_action.txt";
static const size_t kMaxBytes = 256U * 1024U;

static bool FormatEpochTs(uint32_t epoch, char* out, size_t outSize)
{
  if (!out || outSize == 0) return false;
  // epoch here is expected to be "local epoch" (already offset applied by NTP client),
  // so gmtime_r will format it as local wall time.
  time_t tt = (time_t)epoch;
  struct tm tmv;
  if (!gmtime_r(&tt, &tmv)) {
    out[0] = '\0';
    return false;
  }
  snprintf(out, outSize, "%04d-%02d-%02d %02d:%02d:%02d",
           tmv.tm_year + 1900,
           tmv.tm_mon + 1,
           tmv.tm_mday,
           tmv.tm_hour,
           tmv.tm_min,
           tmv.tm_sec);
  return true;
}

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

static void AppendLine(const char* path, const char* name, const char* tag, const char* fmt, va_list ap)
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
    char tsStr[32];
    // If time isn't valid yet, avoid logging "1970..." by falling back to millis().
    if (ts >= 1609459200UL) {
      if (FormatEpochTs(ts, tsStr, sizeof(tsStr))) {
        snprintf(line, sizeof(line), "%s [%s] %s\r\n", tsStr, tag, msg);
      } else {
        snprintf(line, sizeof(line), "%lu [%s] %s\r\n", (unsigned long)ts, tag, msg);
      }
    } else {
      snprintf(line, sizeof(line), "ms=%lu [%s] %s\r\n", (unsigned long)millis(), tag, msg);
    }
  } else {
    snprintf(line, sizeof(line), "ms=%lu [%s] %s\r\n", (unsigned long)millis(), tag, msg);
  }

  File f = LittleFS.open(path, "a");
  if (!f) {
    return;
  }
  f.print(line);
  f.close();

  // Best-effort: send to external sink (do not block / recurse).
  if (g_sink && name && name[0] != '\0') {
    g_sink(name, line);
  }
}

void WS_Log_SetLineSink(WS_LogLineSink sink)
{
  g_sink = sink;
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
  AppendLine(kErrorPath, "error", "ERR", fmt, ap);
  va_end(ap);
}

void WS_Log_Measure(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  AppendLine(kMeasurePath, "measure", "MEAS", fmt, ap);
  va_end(ap);
}

void WS_Log_Action(const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  AppendLine(kActionPath, "action", "ACT", fmt, ap);
  va_end(ap);
}
