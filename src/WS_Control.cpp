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
static uint32_t g_tzOffsetS = 8 * 3600UL;
static uint32_t g_lastTimeOkEpoch = 0;

static void SetDefaults(WS_ControlConfig& cfg)
{
  cfg.tz_offset_s = 8 * 3600UL;
  cfg.mode = WS_CTRL_MIXED;

  cfg.daily_count = 1;
  cfg.daily[0].enabled = true;
  cfg.daily[0].open_enabled = true;
  cfg.daily[0].open_h = 8;
  cfg.daily[0].open_m = 0;
  cfg.daily[0].close_enabled = true;
  cfg.daily[0].close_h = 9;
  cfg.daily[0].close_m = 0;

  cfg.cycle_count = 1;
  cfg.cycle[0].enabled = false;
  cfg.cycle[0].step_count = 3;
  cfg.cycle[0].steps[0].open = true;
  cfg.cycle[0].steps[0].duration_min = 8 * 60;
  cfg.cycle[0].steps[1].open = false;
  cfg.cycle[0].steps[1].duration_min = 3 * 60;
  cfg.cycle[0].steps[2].open = true;
  cfg.cycle[0].steps[2].duration_min = 5 * 60;

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
  doc["tz_offset_s"] = cfg.tz_offset_s;
  doc["mode"] = ModeToStr(cfg.mode);

  JsonArray daily = doc["daily"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.daily_count && i < 8; i++) {
    JsonObject o = daily.add<JsonObject>();
    o["en"] = cfg.daily[i].enabled;
    o["open_en"] = cfg.daily[i].open_enabled;
    o["open"] = String(cfg.daily[i].open_h) + ":" + (cfg.daily[i].open_m < 10 ? "0" : "") + String(cfg.daily[i].open_m);
    o["close_en"] = cfg.daily[i].close_enabled;
    o["close"] = String(cfg.daily[i].close_h) + ":" + (cfg.daily[i].close_m < 10 ? "0" : "") + String(cfg.daily[i].close_m);
  }

  JsonArray cycle = doc["cycle"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.cycle_count && i < 2; i++) {
    JsonObject c = cycle.add<JsonObject>();
    c["en"] = cfg.cycle[i].enabled;
    JsonArray steps = c["steps"].to<JsonArray>();
    for (uint8_t j = 0; j < cfg.cycle[i].step_count && j < 10; j++) {
      JsonObject st = steps.add<JsonObject>();
      st["state"] = cfg.cycle[i].steps[j].open ? "open" : "close";
      st["min"] = cfg.cycle[i].steps[j].duration_min;
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

static bool ParseTimeHHMM(const char* s, uint8_t& h, uint8_t& m)
{
  if (!s) return false;
  int hh = -1, mm = -1;
  if (sscanf(s, "%d:%d", &hh, &mm) != 2) return false;
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
  h = (uint8_t)hh;
  m = (uint8_t)mm;
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

  outCfg.tz_offset_s = doc["tz_offset_s"] | outCfg.tz_offset_s;
  const char* mode = doc["mode"];
  (void)ParseMode(mode, outCfg.mode);

  outCfg.daily_count = 0;
  if (doc["daily"].is<JsonArray>()) {
    for (JsonObject o : doc["daily"].as<JsonArray>()) {
      if (outCfg.daily_count >= 8) break;
      WS_DailyRule& r = outCfg.daily[outCfg.daily_count++];
      r.enabled = o["en"] | false;
      r.open_enabled = o["open_en"] | true;
      r.close_enabled = o["close_en"] | true;
      (void)ParseTimeHHMM(o["open"] | "08:00", r.open_h, r.open_m);
      (void)ParseTimeHHMM(o["close"] | "09:00", r.close_h, r.close_m);
    }
  }

  outCfg.cycle_count = 0;
  if (doc["cycle"].is<JsonArray>()) {
    for (JsonObject c : doc["cycle"].as<JsonArray>()) {
      if (outCfg.cycle_count >= 2) break;
      WS_CycleRule& rule = outCfg.cycle[outCfg.cycle_count++];
      rule.enabled = c["en"] | false;
      rule.step_count = 0;
      if (c["steps"].is<JsonArray>()) {
        for (JsonObject st : c["steps"].as<JsonArray>()) {
          if (rule.step_count >= 10) break;
          const char* state = st["state"] | "open";
          WS_CycleStep& step = rule.steps[rule.step_count++];
          step.open = (strcmp(state, "close") != 0);
          step.duration_min = st["min"] | 60;
          if (step.duration_min == 0) step.duration_min = 1;
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
  g_tzOffsetS = outCfg.tz_offset_s;
  g_ntp.setTimeOffset((long)g_tzOffsetS);
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

void WS_Time_SetTzOffset(uint32_t offset_s)
{
  g_tzOffsetS = offset_s;
  g_ntp.setTimeOffset((long)g_tzOffsetS);
}

void WS_Time_OnWiFiConnected()
{
  g_ntp.begin();
  g_ntp.setTimeOffset((long)g_tzOffsetS);
  g_timeValid = false;
  g_lastTimeOkEpoch = 0;
}

void WS_Time_Loop()
{
  if (WiFi.status() != WL_CONNECTED) {
    g_timeValid = false;
    return;
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
