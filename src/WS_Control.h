#ifndef _WS_CONTROL_H_
#define _WS_CONTROL_H_

#include <Arduino.h>
#include <stdint.h>

// Control modes:
// - daily: fire open/close actions at configured times
// - cycle: run a repeating open/close sequence by durations
// - leveldiff: open when inner<outer, close when inner>=outer (with optional hysteresis)
// - mixed: daily events + otherwise leveldiff (cycle has priority if enabled)
enum WS_CtrlMode : uint8_t {
  WS_CTRL_MIXED = 0,
  WS_CTRL_DAILY = 1,
  WS_CTRL_CYCLE = 2,
  WS_CTRL_LEVELDIFF = 3
};

struct WS_DailyRule {
  bool enabled = false;
  bool open_enabled = true;
  uint8_t open_h = 8;
  uint8_t open_m = 0;
  bool close_enabled = true;
  uint8_t close_h = 9;
  uint8_t close_m = 0;
};

struct WS_CycleStep {
  bool open = true;          // true=open gate, false=close gate
  uint32_t duration_min = 60;
};

struct WS_CycleRule {
  bool enabled = false;
  uint8_t step_count = 0;
  WS_CycleStep steps[10];
};

struct WS_LevelDiffRule {
  bool enabled = false;
  // Open when (inner - outer) <= open_threshold_mm. Default -1 means inner < outer.
  int32_t open_threshold_mm = -1;
  // Close when (inner - outer) >= close_threshold_mm. Default 0 means inner >= outer.
  int32_t close_threshold_mm = 0;
};

struct WS_ControlConfig {
  uint32_t tz_offset_s = 8 * 3600UL;
  WS_CtrlMode mode = WS_CTRL_MIXED;
  uint8_t daily_count = 0;
  WS_DailyRule daily[8];
  uint8_t cycle_count = 0;
  WS_CycleRule cycle[2];
  uint8_t leveldiff_count = 0;
  WS_LevelDiffRule leveldiff[4];
};

// Config file stored on ESP32S3 LittleFS.
bool WS_Control_Load(WS_ControlConfig& outCfg);
bool WS_Control_Save(const WS_ControlConfig& cfg);
bool WS_Control_SaveRawJson(const char* json);
String WS_Control_LoadRawJson();

// Runtime helpers
bool WS_Time_IsValid();
uint32_t WS_Time_NowEpoch();
void WS_Time_SetTzOffset(uint32_t offset_s);
void WS_Time_OnWiFiConnected();   // call after Wi-Fi connects
void WS_Time_Loop();              // call in main loop

#endif
