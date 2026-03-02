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
  // Monday..Sunday bitmask (bit0=Mon ... bit6=Sun). 0 means "never".
  uint8_t dow_mask = 0x7F;
  bool open_enabled = true;
  // Milliseconds since 00:00 (local time). UI typically uses minute precision.
  uint32_t open_ms = 8UL * 3600UL * 1000UL;
  bool close_enabled = true;
  uint32_t close_ms = 9UL * 3600UL * 1000UL;
};

struct WS_CycleStep {
  bool open = true;          // true=open gate, false=close gate
  uint32_t duration_ms = 60UL * 60UL * 1000UL;
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
  // Timezone offset in milliseconds. Example: UTC+8 => 28800000.
  int32_t tz_offset_ms = 8 * 3600L * 1000L;
  WS_CtrlMode mode = WS_CTRL_MIXED;
  uint8_t daily_count = 0;
  WS_DailyRule daily[8];
  uint8_t cycle_count = 0;
  WS_CycleRule cycle[5];
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
void WS_Time_SetTzOffsetMs(int32_t offset_ms);
void WS_Time_OnWiFiConnected();   // call after Wi-Fi connects
void WS_Time_Loop();              // call in main loop

#endif
