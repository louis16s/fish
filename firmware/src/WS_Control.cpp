#include "WS_Control.h"
#include "WS_FS.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <cstring>

static const char* kCtrlPath = "/ctrl.json";

static WiFiUDP g_udp;
static NTPClient g_ntp(g_udp, "pool.ntp.org");
static bool g_timeValid = false;
static int32_t g_tzOffsetMs = 8 * 3600L * 1000L;
static uint32_t g_lastTimeOkEpoch = 0;
static bool g_ntpStarted = false;

static void SetDefaults(WS_ControlConfig& cfg)
{
  cfg.tz_offset_ms = 8 * 3600L * 1000L;
  cfg.mode = WS_CTRL_MIXED;

  cfg.daily_count = 1;
  cfg.daily[0].enabled = true;
  cfg.daily[0].dow_mask = 0x7F;
  cfg.daily[0].open_enabled = true;
  cfg.daily[0].open_ms = 8UL * 3600UL * 1000UL;
  cfg.daily[0].close_enabled = true;
  cfg.daily[0].close_ms = 9UL * 3600UL * 1000UL;

  cfg.cycle_count = 1;
  cfg.cycle[0].enabled = false;
  cfg.cycle[0].step_count = 3;
  cfg.cycle[0].steps[0].open = true;
  cfg.cycle[0].steps[0].duration_ms = 8UL * 3600UL * 1000UL;
  cfg.cycle[0].steps[1].open = false;
  cfg.cycle[0].steps[1].duration_ms = 3UL * 3600UL * 1000UL;
  cfg.cycle[0].steps[2].open = true;
  cfg.cycle[0].steps[2].duration_ms = 5UL * 3600UL * 1000UL;

  cfg.leveldiff_count = 1;
  cfg.leveldiff[0].enabled = true;
  cfg.leveldiff[0].open_threshold_mm = -1;
  cfg.leveldiff[0].close_threshold_mm = 0;
}

static bool ParseMode(const char* s, WS_CtrlMode& out)
{
  if (!s) return false;
  if (!strcmp(s, "mixed")) { out = WS_CTRL_MIXED; return true; }
  if (!strcmp(s, "daily")) { out = WS_CTRL_DAILY; return true; }
  if (!strcmp(s, "cycle")) { out = WS_CTRL_CYCLE; return true; }
  if (!strcmp(s, "leveldiff")) { out = WS_CTRL_LEVELDIFF; return true; }
  return false;
}

static const char* ModeToStr(WS_CtrlMode m)
{
  switch (m) {
    case WS_CTRL_DAILY: return "daily";
    case WS_CTRL_CYCLE: return "cycle";
    case WS_CTRL_LEVELDIFF: return "leveldiff";
    default: return "mixed";
  }
}

static bool SaveToFS(const String& content)
{
  if (!WS_FS_EnsureMounted()) return false;
  File f = LittleFS.open(kCtrlPath, "w");
  if (!f) return false;
  f.print(content);
  f.close();
  return true;
}

String WS_Control_LoadRawJson()
{
  if (!WS_FS_EnsureMounted()) return "";
  if (!LittleFS.exists(kCtrlPath)) return "";
  File f = LittleFS.open(kCtrlPath, "r");
  if (!f) return "";
  String s = f.readString();
  f.close();
  return s;
}

bool WS_Control_SaveRawJson(const char* json)
{
  if (!json) return false;
  // validate json
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    return false;
  }
  String s;
  serializeJsonPretty(doc, s);
  return SaveToFS(s);
}

bool WS_Control_Save(const WS_ControlConfig& cfg)
{
  JsonDocument doc;
  doc["tz_offset_ms"] = cfg.tz_offset_ms;
  doc["mode"] = ModeToStr(cfg.mode);

  JsonArray daily = doc["daily"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.daily_count && i < 8; i++) {
    JsonObject o = daily.add<JsonObject>();
    o["en"] = cfg.daily[i].enabled;
    o["dow_mask"] = cfg.daily[i].dow_mask;
    o["open_en"] = cfg.daily[i].open_enabled;
    o["open_ms"] = cfg.daily[i].open_ms;
    o["close_en"] = cfg.daily[i].close_enabled;
    o["close_ms"] = cfg.daily[i].close_ms;
  }

  JsonArray cycle = doc["cycle"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.cycle_count && i < 5; i++) {
    JsonObject c = cycle.add<JsonObject>();
    c["en"] = cfg.cycle[i].enabled;
    JsonArray steps = c["steps"].to<JsonArray>();
    for (uint8_t j = 0; j < cfg.cycle[i].step_count && j < 10; j++) {
      JsonObject st = steps.add<JsonObject>();
      st["state"] = cfg.cycle[i].steps[j].open ? "open" : "close";
      st["dur_ms"] = cfg.cycle[i].steps[j].duration_ms;
    }
  }

  JsonArray ld = doc["leveldiff"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.leveldiff_count && i < 4; i++) {
    JsonObject o = ld.add<JsonObject>();
    o["en"] = cfg.leveldiff[i].enabled;
    o["open_mm"] = cfg.leveldiff[i].open_threshold_mm;
    o["close_mm"] = cfg.leveldiff[i].close_threshold_mm;
  }

  String out;
  serializeJsonPretty(doc, out);
  return SaveToFS(out);
}

static bool ParseTimeHHMMToMs(const char* s, uint32_t& outMs)
{
  outMs = 0;
  if (!s) return false;
  int hh = -1, mm = -1;
  if (sscanf(s, "%d:%d", &hh, &mm) != 2) return false;
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
  outMs = (uint32_t)((hh * 3600L + mm * 60L) * 1000L);
  return true;
}

bool WS_Control_Load(WS_ControlConfig& outCfg)
{
  SetDefaults(outCfg);

  String raw = WS_Control_LoadRawJson();
  if (raw.length() == 0) {
    (void)WS_Control_Save(outCfg);
    return true;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, raw);
  if (err) {
    // keep defaults if corrupted
    return false;
  }

  if (!doc["tz_offset_ms"].isNull()) {
    outCfg.tz_offset_ms = doc["tz_offset_ms"] | outCfg.tz_offset_ms;
  } else if (!doc["tz_offset_s"].isNull()) {
    const uint32_t s = doc["tz_offset_s"] | (uint32_t)(outCfg.tz_offset_ms / 1000L);
    outCfg.tz_offset_ms = (int32_t)(s * 1000UL);
  }
  const char* mode = doc["mode"];
  (void)ParseMode(mode, outCfg.mode);

  outCfg.daily_count = 0;
  if (doc["daily"].is<JsonArray>()) {
    for (JsonObject o : doc["daily"].as<JsonArray>()) {
      if (outCfg.daily_count >= 8) break;
      WS_DailyRule& r = outCfg.daily[outCfg.daily_count++];
      r.enabled = o["en"] | false;
      const uint32_t mask = (uint32_t)(o["dow_mask"] | 0x7FU) & 0x7FU;
      r.dow_mask = (uint8_t)mask;
      r.open_enabled = o["open_en"] | true;
      r.close_enabled = o["close_en"] | true;
      // New schema: open_ms/close_ms. Backward compat: "open":"HH:MM" / "close":"HH:MM".
      r.open_ms = o["open_ms"] | r.open_ms;
      r.close_ms = o["close_ms"] | r.close_ms;
      if (o["open_ms"].isNull()) {
        uint32_t tmp = 0;
        if (ParseTimeHHMMToMs(o["open"] | "08:00", tmp)) r.open_ms = tmp;
      }
      if (o["close_ms"].isNull()) {
        uint32_t tmp = 0;
        if (ParseTimeHHMMToMs(o["close"] | "09:00", tmp)) r.close_ms = tmp;
      }
    }
  }

  outCfg.cycle_count = 0;
  if (doc["cycle"].is<JsonArray>()) {
    for (JsonObject c : doc["cycle"].as<JsonArray>()) {
      if (outCfg.cycle_count >= 5) break;
      WS_CycleRule& rule = outCfg.cycle[outCfg.cycle_count++];
      rule.enabled = c["en"] | false;
      rule.step_count = 0;
      if (c["steps"].is<JsonArray>()) {
        for (JsonObject st : c["steps"].as<JsonArray>()) {
          if (rule.step_count >= 10) break;
          const char* state = st["state"] | "open";
          WS_CycleStep& step = rule.steps[rule.step_count++];
          step.open = (strcmp(state, "close") != 0);
          if (!st["dur_ms"].isNull()) {
            step.duration_ms = st["dur_ms"] | step.duration_ms;
          } else if (!st["min"].isNull()) {
            const uint32_t min = st["min"] | 60;
            step.duration_ms = min * 60UL * 1000UL;
          } else if (!st["ms"].isNull()) {
            step.duration_ms = st["ms"] | step.duration_ms;
          }
          if (step.duration_ms == 0) step.duration_ms = 1000UL;
        }
      }
    }
  }

  outCfg.leveldiff_count = 0;
  if (doc["leveldiff"].is<JsonArray>()) {
    for (JsonObject o : doc["leveldiff"].as<JsonArray>()) {
      if (outCfg.leveldiff_count >= 4) break;
      WS_LevelDiffRule& r = outCfg.leveldiff[outCfg.leveldiff_count++];
      r.enabled = o["en"] | false;
      r.open_threshold_mm = o["open_mm"] | -1;
      r.close_threshold_mm = o["close_mm"] | 0;
    }
  }

  // Apply tz to NTP module
  g_tzOffsetMs = outCfg.tz_offset_ms;
  g_ntp.setTimeOffset((long)(g_tzOffsetMs / 1000L));
  return true;
}

bool WS_Time_IsValid()
{
  return g_timeValid;
}

uint32_t WS_Time_NowEpoch()
{
  return g_ntp.getEpochTime();
}

void WS_Time_SetTzOffsetMs(int32_t offset_ms)
{
  g_tzOffsetMs = offset_ms;
  g_ntp.setTimeOffset((long)(g_tzOffsetMs / 1000L));
}

void WS_Time_OnWiFiConnected()
{
  g_ntp.begin();
  g_ntp.setTimeOffset((long)(g_tzOffsetMs / 1000L));
  g_timeValid = false;
  g_lastTimeOkEpoch = 0;
  g_ntpStarted = true;
}

void WS_Time_Loop()
{
  if (WiFi.status() != WL_CONNECTED) {
    g_timeValid = false;
    return;
  }

  // In case the caller didn't notify connection (or Wi-Fi reconnected),
  // ensure NTP client is started.
  if (!g_ntpStarted) {
    g_ntp.begin();
    g_ntp.setTimeOffset((long)(g_tzOffsetMs / 1000L));
    g_ntpStarted = true;
  }

  if (!g_ntp.update()) {
    g_ntp.forceUpdate();
  }

  const uint32_t e = g_ntp.getEpochTime();
  // 2021-01-01 00:00:00 UTC
  if (e >= 1609459200UL) {
    g_timeValid = true;
    g_lastTimeOkEpoch = e;
  } else {
    g_timeValid = false;
  }
}
