# 1 "C:\\Users\\louis16s\\AppData\\Local\\Temp\\tmp7xsfwuri"
#include <Arduino.h>
# 1 "D:/users/code/fish-github/src/MAIN_ALL.ino"
#include <HardwareSerial.h>
#include <stdarg.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include "WS_MQTT.h"
#include "WS_GPIO.h"
#include "WS_Serial.h"
#include "WS_Information.h"
#include "WS_Control.h"
#include "WS_Log.h"

#define CH1 '1'
#define CH2 '2'
#define CH3 '3'
#define CH4 '4'
#define CH5 '5'
#define CH6 '6'
#define GATE_STOP '0'
#define ALL_ON '7'
#define ALL_OFF '8'

#define RS485_Mode 1
extern char ipStr[16];
extern bool WIFI_Connection;
extern HardwareSerial lidarSerial;

bool Relay_Flag[6] = {0};


const uint8_t GATE_STATE_STOPPED = 0;
const uint8_t GATE_STATE_OPENING = 1;
const uint8_t GATE_STATE_CLOSING = 2;
uint8_t Gate_State = GATE_STATE_STOPPED;
bool Gate_Position_Open = false;
bool Gate_AutoControl_Enabled = GATE_AUTO_CONTROL_Enable;
bool Gate_Auto_Latched_Off = false;
bool Gate_Action_Active = false;
uint32_t Gate_Action_StartMs = 0;
const uint32_t GATE_ACTION_DURATION_MS = (uint32_t)GATE_RELAY_ACTION_SECONDS * 1000UL;


const uint8_t SENSOR_ID_1 = INNER_POND_SENSOR_ID;
const uint8_t SENSOR_ID_2 = OUTER_POND_SENSOR_ID;
const uint16_t SENSOR_POLL_INTERVAL_MS = 1000;

uint16_t Sensor_Level_mm_1 = 0;
uint16_t Sensor_Level_mm_2 = 0;
int16_t Sensor_Temp_x10_1 = 0;
int16_t Sensor_Temp_x10_2 = 0;
bool Sensor_Online_1 = false;
bool Sensor_Online_2 = false;
bool Sensor_HasValue_1 = false;
bool Sensor_HasValue_2 = false;
bool Sensor_HasTemp_1 = false;
bool Sensor_HasTemp_2 = false;
uint32_t Last_Sensor_Poll_Ms = 0;
uint8_t Next_Sensor_ID = SENSOR_ID_1;
uint32_t Last_Level_Log_Ms = 0;
uint32_t Sensor_Last_Ok_Ms_1 = 0;
uint32_t Sensor_Last_Ok_Ms_2 = 0;
uint16_t Sensor_Prev_Level_mm_1 = 0;
uint16_t Sensor_Prev_Level_mm_2 = 0;
uint32_t Sensor_Prev_Level_Ms_1 = 0;
uint32_t Sensor_Prev_Level_Ms_2 = 0;
bool Sensor_Prev_Valid_1 = false;
bool Sensor_Prev_Valid_2 = false;

uint32_t Gate_Last_Action_EndMs = 0;
bool Gate_Open_Allowed = true;
bool Gate_Close_Allowed = true;
char Gate_Block_Reason[96] = "";

bool Manual_Takeover_Active = false;
uint32_t Manual_Takeover_UntilMs = 0;
uint32_t Manual_Takeover_DurationMs = 0;

bool Alarm_Active = false;
uint8_t Alarm_Severity = 0;
char Alarm_Text[128] = "normal";
bool Alarm_GateTimeout = false;
bool Alarm_RelayInterlock = false;
uint32_t Alarm_LevelJump_ExpireMs = 0;
uint32_t Alarm_LevelRange_ExpireMs = 0;
uint32_t Gate_Last_Block_Log_Ms = 0;

static WS_ControlConfig CtrlCfg;
static bool CtrlCfgLoaded = false;


static int32_t Daily_Last_Fired_DayKey_Open[8] = {0};
static int32_t Daily_Last_Fired_DayKey_Close[8] = {0};


static uint8_t Cycle_ActiveRule = 0;
static uint8_t Cycle_StepIndex = 0;
static uint32_t Cycle_StepEndMs = 0;


static uint32_t Log_LastMeasureMs = 0;
static const uint32_t LOG_MEASURE_INTERVAL_MS = 60000UL;

static bool Alarm_PrevActive = false;
static uint8_t Alarm_PrevSeverity = 0;
static char Alarm_PrevText[128] = "normal";
static void Gate_Log(const char* fmt, ...);
static bool Gate_Should_Log_Block();
static void Gate_Stop();
static bool Gate_Open();
static bool Gate_Close();
static void Gate_Action_Loop();
static void Gate_AutoControl_Loop();
static void Ctrl_LoadIfNeeded();
static int32_t DayKeyFromEpoch(uint32_t epoch);
static void Ctrl_Daily_Loop(uint32_t curMinOfDay, int32_t dayKey);
static void Ctrl_Cycle_Loop();
static void Ctrl_LevelDiff_Loop();
static void Ctrl_Automation_Loop();
static void Manual_Takeover_Loop();
static void Alarm_LogTransitions();
static uint16_t Modbus_CRC16(const uint8_t* data, size_t len);
static bool Read_Sensor_Data(uint8_t id, uint16_t* level_mm, int16_t* temp_x10);
static bool Read_Sensor_WithRetry(uint8_t id, uint16_t* level_mm, int16_t* temp_x10);
static void Sensor_Read_Loop();
void Relay_Analysis(uint8_t *buf,uint8_t Mode_Flag);
void setup();
void loop();
#line 108 "D:/users/code/fish-github/src/MAIN_ALL.ino"
static void Gate_Log(const char* fmt, ...)
{
  if (!SERIAL_GATE_LOG_Enable) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

static bool Gate_Should_Log_Block()
{
  if (!SERIAL_GATE_LOG_Enable) {
    return false;
  }
  const uint32_t now = millis();
  if ((now - Gate_Last_Block_Log_Ms) < (uint32_t)SERIAL_GATE_LOG_BLOCK_INTERVAL_MS) {
    return false;
  }
  Gate_Last_Block_Log_Ms = now;
  return true;
}

static void Update_Alarm_Status();
static void Update_Gate_Command_Availability();
void Set_Manual_Takeover(uint32_t duration_ms);
void End_Manual_Takeover();
void Enable_Auto_Mode();
void Latch_Auto_Off();
void Pause_Auto_By_ManualTakeover();

static void Offline_Network_RGB_Loop();

static void Gate_Stop()
{
  digitalWrite(GPIO_PIN_CH1, LOW);
  digitalWrite(GPIO_PIN_CH2, LOW);
  Relay_Flag[0] = 0;
  Relay_Flag[1] = 0;
  Gate_State = GATE_STATE_STOPPED;
  Gate_Action_Active = false;
  Gate_Last_Action_EndMs = millis();
  Update_Gate_Command_Availability();
  WS_Log_Action("gate_stop");
}

static bool Gate_Open()
{
  if (Relay_Flag[1]) {
    if (Manual_Takeover_Active) {

      Gate_Stop();
    } else {
      snprintf(Gate_Block_Reason, sizeof(Gate_Block_Reason), "Interlock: close relay is active");
      Update_Gate_Command_Availability();
      WS_Log_Action("gate_open_blocked: %s", Gate_Block_Reason);
      return false;
    }
  }
  if (Gate_State == GATE_STATE_OPENING) {
    snprintf(Gate_Block_Reason, sizeof(Gate_Block_Reason), "Gate is already opening");
    Update_Gate_Command_Availability();
    WS_Log_Action("gate_open_blocked: %s", Gate_Block_Reason);
    return false;
  }
  if (!Manual_Takeover_Active && (millis() - Gate_Last_Action_EndMs < (uint32_t)GATE_MIN_ACTION_INTERVAL_S * 1000UL)) {
    snprintf(Gate_Block_Reason, sizeof(Gate_Block_Reason), "Cooldown active: wait before next action");
    Update_Gate_Command_Availability();
    WS_Log_Action("gate_open_blocked: %s", Gate_Block_Reason);
    return false;
  }
  digitalWrite(GPIO_PIN_CH2, LOW);
  digitalWrite(GPIO_PIN_CH1, HIGH);
  Relay_Flag[1] = 0;
  Relay_Flag[0] = 1;
  Gate_State = GATE_STATE_OPENING;
  Gate_Action_Active = true;
  Gate_Action_StartMs = millis();
  Alarm_GateTimeout = false;
  Alarm_RelayInterlock = false;
  Update_Gate_Command_Availability();
  WS_Log_Action("gate_open_start");
  return true;
}

static bool Gate_Close()
{
  if (Relay_Flag[0]) {
    if (Manual_Takeover_Active) {

      Gate_Stop();
    } else {
      snprintf(Gate_Block_Reason, sizeof(Gate_Block_Reason), "Interlock: open relay is active");
      Update_Gate_Command_Availability();
      WS_Log_Action("gate_close_blocked: %s", Gate_Block_Reason);
      return false;
    }
  }
  if (Gate_State == GATE_STATE_CLOSING) {
    snprintf(Gate_Block_Reason, sizeof(Gate_Block_Reason), "Gate is already closing");
    Update_Gate_Command_Availability();
    WS_Log_Action("gate_close_blocked: %s", Gate_Block_Reason);
    return false;
  }
  if (!Manual_Takeover_Active && (millis() - Gate_Last_Action_EndMs < (uint32_t)GATE_MIN_ACTION_INTERVAL_S * 1000UL)) {
    snprintf(Gate_Block_Reason, sizeof(Gate_Block_Reason), "Cooldown active: wait before next action");
    Update_Gate_Command_Availability();
    WS_Log_Action("gate_close_blocked: %s", Gate_Block_Reason);
    return false;
  }
  digitalWrite(GPIO_PIN_CH1, LOW);
  digitalWrite(GPIO_PIN_CH2, HIGH);
  Relay_Flag[0] = 0;
  Relay_Flag[1] = 1;
  Gate_State = GATE_STATE_CLOSING;
  Gate_Action_Active = true;
  Gate_Action_StartMs = millis();
  Alarm_GateTimeout = false;
  Alarm_RelayInterlock = false;
  Update_Gate_Command_Availability();
  WS_Log_Action("gate_close_start");
  return true;
}

static void Gate_Action_Loop()
{
  if (!Gate_Action_Active) {
    return;
  }
  if ((millis() - Gate_Action_StartMs) > ((uint32_t)GATE_MAX_CONTINUOUS_RUN_S * 1000UL)) {
    Alarm_GateTimeout = true;
    snprintf(Alarm_Text, sizeof(Alarm_Text), "Alarm: gate runtime timeout, stopped");
    Gate_Stop();
    return;
  }
  if (millis() - Gate_Action_StartMs < GATE_ACTION_DURATION_MS) {
    return;
  }
  if (Gate_State == GATE_STATE_OPENING) {
    Gate_Position_Open = true;
  } else if (Gate_State == GATE_STATE_CLOSING) {
    Gate_Position_Open = false;
  }
  Gate_Stop();
}

static void Gate_AutoControl_Loop()
{

}

static void Ctrl_LoadIfNeeded()
{
  if (CtrlCfgLoaded) {
    return;
  }
  CtrlCfgLoaded = WS_Control_Load(CtrlCfg);
  WS_Time_SetTzOffsetMs(CtrlCfg.tz_offset_ms);
}

static int32_t DayKeyFromEpoch(uint32_t epoch)
{

  return (int32_t)(epoch / 86400UL);
}

static void Ctrl_Daily_Loop(uint32_t curMinOfDay, int32_t dayKey)
{
  if (CtrlCfg.daily_count == 0) return;

  const uint8_t dow = (uint8_t)((dayKey + 3) % 7);
  const uint8_t dowBit = (uint8_t)(1U << dow);
  for (uint8_t i = 0; i < CtrlCfg.daily_count && i < 8; i++) {
    const WS_DailyRule& r = CtrlCfg.daily[i];
    if (!r.enabled) continue;
    if ((r.dow_mask & dowBit) == 0) continue;

    const uint32_t openMin = (uint32_t)(r.open_ms / 60000UL);
    const uint32_t closeMin = (uint32_t)(r.close_ms / 60000UL);

    if (r.open_enabled && curMinOfDay == openMin) {
      if (Daily_Last_Fired_DayKey_Open[i] != dayKey) {
        Daily_Last_Fired_DayKey_Open[i] = dayKey;
        const uint32_t hh = openMin / 60UL;
        const uint32_t mm = openMin % 60UL;
        WS_Log_Action("daily[%u] fire open %02lu:%02lu", (unsigned)i, (unsigned long)hh, (unsigned long)mm);
        Gate_Open();
      }
    }

    if (r.close_enabled && curMinOfDay == closeMin) {
      if (Daily_Last_Fired_DayKey_Close[i] != dayKey) {
        Daily_Last_Fired_DayKey_Close[i] = dayKey;
        const uint32_t hh = closeMin / 60UL;
        const uint32_t mm = closeMin % 60UL;
        WS_Log_Action("daily[%u] fire close %02lu:%02lu", (unsigned)i, (unsigned long)hh, (unsigned long)mm);
        Gate_Close();
      }
    }
  }
}

static const WS_CycleRule* Ctrl_FindActiveCycleRule()
{
  for (uint8_t i = 0; i < CtrlCfg.cycle_count && i < 5; i++) {
    if (CtrlCfg.cycle[i].enabled && CtrlCfg.cycle[i].step_count > 0) {
      Cycle_ActiveRule = i;
      return &CtrlCfg.cycle[i];
    }
  }
  return nullptr;
}

static void Ctrl_Cycle_Loop()
{
  const WS_CycleRule* rule = Ctrl_FindActiveCycleRule();
  if (!rule) {
    Cycle_StepEndMs = 0;
    Cycle_StepIndex = 0;
    return;
  }

  if (Gate_Action_Active) {
    return;
  }

  const uint32_t now = millis();
  if (Cycle_StepEndMs == 0) {
    Cycle_StepIndex = 0;
    const WS_CycleStep& st = rule->steps[Cycle_StepIndex];
    WS_Log_Action("cycle start step=%u state=%s dur_ms=%lu", (unsigned)Cycle_StepIndex, st.open ? "open" : "close", (unsigned long)st.duration_ms);
    (st.open ? Gate_Open() : Gate_Close());
    Cycle_StepEndMs = now + st.duration_ms;
    return;
  }

  if ((int32_t)(now - Cycle_StepEndMs) < 0) {
    return;
  }

  Cycle_StepIndex = (uint8_t)((Cycle_StepIndex + 1U) % rule->step_count);
  const WS_CycleStep& st = rule->steps[Cycle_StepIndex];
  WS_Log_Action("cycle next step=%u state=%s dur_ms=%lu", (unsigned)Cycle_StepIndex, st.open ? "open" : "close", (unsigned long)st.duration_ms);
  (st.open ? Gate_Open() : Gate_Close());
  Cycle_StepEndMs = now + st.duration_ms;
}

static void Ctrl_LevelDiff_Loop()
{
  if (!Sensor_HasValue_1 || !Sensor_HasValue_2) {
    return;
  }
  if (CtrlCfg.leveldiff_count == 0) {
    return;
  }


  const WS_LevelDiffRule* pr = nullptr;
  uint8_t ridx = 0;
  for (uint8_t i = 0; i < CtrlCfg.leveldiff_count && i < 4; i++) {
    if (CtrlCfg.leveldiff[i].enabled) {
      pr = &CtrlCfg.leveldiff[i];
      ridx = i;
      break;
    }
  }
  if (!pr) return;
  const WS_LevelDiffRule& r = *pr;
  if (Gate_Action_Active) {
    return;
  }

  const int32_t delta = (int32_t)Sensor_Level_mm_1 - (int32_t)Sensor_Level_mm_2;
  if (delta <= r.open_threshold_mm) {
    if (!Gate_Position_Open) {
      WS_Log_Action("leveldiff[%u] open delta=%ld <= %ld", (unsigned)ridx, (long)delta, (long)r.open_threshold_mm);
      Gate_Open();
    }
    return;
  }

  if (delta >= r.close_threshold_mm) {
    if (Gate_Position_Open) {
      WS_Log_Action("leveldiff[%u] close delta=%ld >= %ld", (unsigned)ridx, (long)delta, (long)r.close_threshold_mm);
      Gate_Close();
    }
  }
}

static void Ctrl_Automation_Loop()
{
  if (!Gate_AutoControl_Enabled || Gate_Auto_Latched_Off) {
    return;
  }
  if (Manual_Takeover_Active) {
    return;
  }
  if (!CtrlCfgLoaded) {
    Ctrl_LoadIfNeeded();
  }


  if (CtrlCfg.mode == WS_CTRL_CYCLE) {
    Ctrl_Cycle_Loop();
    return;
  }
  if (CtrlCfg.mode == WS_CTRL_DAILY) {
    if (WS_Time_IsValid()) {
      const uint32_t e = WS_Time_NowEpoch();
      const uint32_t minOfDay = (uint32_t)((e % 86400UL) / 60UL);
      const int32_t dayKey = DayKeyFromEpoch(e);
      Ctrl_Daily_Loop(minOfDay, dayKey);
    }
    return;
  }
  if (CtrlCfg.mode == WS_CTRL_LEVELDIFF) {
    Ctrl_LevelDiff_Loop();
    return;
  }


  if (Ctrl_FindActiveCycleRule() != nullptr) {
    Ctrl_Cycle_Loop();
    return;
  }
  if (WS_Time_IsValid()) {
    const uint32_t e = WS_Time_NowEpoch();
    const uint32_t minOfDay = (uint32_t)((e % 86400UL) / 60UL);
    const int32_t dayKey = DayKeyFromEpoch(e);
    Ctrl_Daily_Loop(minOfDay, dayKey);
  }
  Ctrl_LevelDiff_Loop();
}

void Set_Manual_Takeover(uint32_t duration_ms)
{
  if (duration_ms == 0) {
    return;
  }
  Manual_Takeover_Active = true;
  Manual_Takeover_UntilMs = millis() + duration_ms;
  Manual_Takeover_DurationMs = duration_ms;
}

void End_Manual_Takeover()
{
  Manual_Takeover_Active = false;
  Manual_Takeover_UntilMs = 0;
  Manual_Takeover_DurationMs = 0;
}

void Enable_Auto_Mode()
{
  Gate_Auto_Latched_Off = false;
  Gate_AutoControl_Enabled = true;
  End_Manual_Takeover();
}

void Latch_Auto_Off()
{
  Gate_Auto_Latched_Off = true;
  Gate_AutoControl_Enabled = false;
  End_Manual_Takeover();
}

void Pause_Auto_By_ManualTakeover()
{
  Gate_Auto_Latched_Off = false;
  Gate_AutoControl_Enabled = true;
  Set_Manual_Takeover((uint32_t)MANUAL_TAKEOVER_RECOVER_S * 1000UL);
}

static void Manual_Takeover_Loop()
{
  if (!Manual_Takeover_Active) {
    return;
  }
  if ((int32_t)(millis() - Manual_Takeover_UntilMs) >= 0) {
    End_Manual_Takeover();
  }
}

static void Update_Gate_Command_Availability()
{
  Gate_Open_Allowed = true;
  Gate_Close_Allowed = true;
  snprintf(Gate_Block_Reason, sizeof(Gate_Block_Reason), "");

  if (Relay_Flag[0] && Relay_Flag[1]) {
    Gate_Open_Allowed = false;
    Gate_Close_Allowed = false;
    snprintf(Gate_Block_Reason, sizeof(Gate_Block_Reason), "Interlock: both relays cannot be active");
    return;
  }

  if (Manual_Takeover_Active) {
    return;
  }
  if (Gate_Action_Active) {
    Gate_Open_Allowed = false;
    Gate_Close_Allowed = false;
    snprintf(Gate_Block_Reason, sizeof(Gate_Block_Reason), "Gate is running, repeat action blocked");
    return;
  }
  if (millis() - Gate_Last_Action_EndMs < (uint32_t)GATE_MIN_ACTION_INTERVAL_S * 1000UL) {
    Gate_Open_Allowed = false;
    Gate_Close_Allowed = false;
    snprintf(Gate_Block_Reason, sizeof(Gate_Block_Reason), "Cooldown active: min action interval");
    return;
  }
}

static void Update_Alarm_Status()
{
  const uint32_t now = millis();
  const bool sensor1_stale = (Sensor_Last_Ok_Ms_1 == 0) || ((now - Sensor_Last_Ok_Ms_1) > SENSOR_DATA_TIMEOUT_MS);
  const bool sensor2_stale = (Sensor_Last_Ok_Ms_2 == 0) || ((now - Sensor_Last_Ok_Ms_2) > SENSOR_DATA_TIMEOUT_MS);
  const bool alarm_sensor_timeout = sensor1_stale || sensor2_stale;
  const bool alarm_level_jump = now < Alarm_LevelJump_ExpireMs;
  const bool alarm_level_range = now < Alarm_LevelRange_ExpireMs;
  const bool alarm_interlock = Alarm_RelayInterlock;
  const bool alarm_gate_timeout = Alarm_GateTimeout;

  Alarm_Active = false;
  Alarm_Severity = 0;
  snprintf(Alarm_Text, sizeof(Alarm_Text), "normal");

  if (alarm_interlock || alarm_gate_timeout || alarm_sensor_timeout) {
    Alarm_Active = true;
    Alarm_Severity = 2;
    if (alarm_interlock) {
      snprintf(Alarm_Text, sizeof(Alarm_Text), "Alarm: relay interlock triggered");
    } else if (alarm_gate_timeout) {
      snprintf(Alarm_Text, sizeof(Alarm_Text), "Alarm: gate execution timeout");
    } else {
      snprintf(Alarm_Text, sizeof(Alarm_Text), "Alarm: sensor offline or data timeout");
    }
    return;
  }

  if (alarm_level_jump || alarm_level_range) {
    Alarm_Active = true;
    Alarm_Severity = 1;
    if (alarm_level_jump) {
      snprintf(Alarm_Text, sizeof(Alarm_Text), "Warning: abnormal level jump");
    } else {
      snprintf(Alarm_Text, sizeof(Alarm_Text), "Warning: level out of safe range");
    }
    return;
  }
}

static void Alarm_LogTransitions()
{
  if (Alarm_Active != Alarm_PrevActive || Alarm_Severity != Alarm_PrevSeverity || strcmp(Alarm_Text, Alarm_PrevText) != 0) {

    const bool prevSerious = Alarm_PrevActive && (Alarm_PrevSeverity >= 2);
    const bool curSerious = Alarm_Active && (Alarm_Severity >= 2);
    if (curSerious) {
      WS_Log_Error("alarm severity=%u text=%s", (unsigned)Alarm_Severity, Alarm_Text);
    } else if (prevSerious && !curSerious) {
      WS_Log_Error("alarm cleared");
    }
    Alarm_PrevActive = Alarm_Active;
    Alarm_PrevSeverity = Alarm_Severity;
    snprintf(Alarm_PrevText, sizeof(Alarm_PrevText), "%s", Alarm_Text);
  }
}

static uint16_t Modbus_CRC16(const uint8_t* data, size_t len)
{
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

static bool Read_Sensor_Data(uint8_t id, uint16_t* level_mm, int16_t* temp_x10)
{

  uint8_t req[8] = {id, 0x03, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00};
  const uint16_t req_crc = Modbus_CRC16(req, 6);
  req[6] = req_crc & 0xFF;
  req[7] = (req_crc >> 8) & 0xFF;



  while (lidarSerial.available() > 0) {
    lidarSerial.read();
  }
  delay(5);
  while (lidarSerial.available() > 0) {
    lidarSerial.read();
  }

  lidarSerial.write(req, sizeof(req));
  lidarSerial.flush();




  uint8_t raw[32] = {0};
  size_t received = 0;
  const uint32_t start_ms = millis();

  while ((millis() - start_ms) < (uint32_t)SENSOR_MODBUS_TIMEOUT_MS) {
    bool progressed = false;
    while (lidarSerial.available() > 0 && received < sizeof(raw)) {
      raw[received++] = (uint8_t)lidarSerial.read();
      progressed = true;
    }

    if (received >= 13) {
      for (size_t off = 0; off + 13 <= received; ++off) {
        if (raw[off] != id || raw[off + 1] != 0x03 || raw[off + 2] != 0x08) {
          continue;
        }
        const uint16_t resp_crc = (uint16_t)raw[off + 11] | ((uint16_t)raw[off + 12] << 8);
        const uint16_t calc_crc = Modbus_CRC16(&raw[off], 11);
        if (resp_crc != calc_crc) {
          continue;
        }
        *level_mm = ((uint16_t)raw[off + 3] << 8) | raw[off + 4];
        *temp_x10 = (int16_t)(((uint16_t)raw[off + 9] << 8) | raw[off + 10]);
        return true;
      }
    }

    if (received >= sizeof(raw)) {
      break;
    }
    if (!progressed) {
      delay(1);
    }
  }

  return false;
}

static bool Read_Sensor_WithRetry(uint8_t id, uint16_t* level_mm, int16_t* temp_x10)
{
  for (uint8_t attempt = 0; attempt < (uint8_t)SENSOR_MODBUS_RETRY_COUNT; ++attempt) {
    if (Read_Sensor_Data(id, level_mm, temp_x10)) {
      return true;
    }
    if ((attempt + 1U) < (uint8_t)SENSOR_MODBUS_RETRY_COUNT) {
      delay(SENSOR_MODBUS_RETRY_GAP_MS);
    }
  }
  return false;
}

static void Sensor_Read_Loop()
{
  if (millis() - Last_Sensor_Poll_Ms < SENSOR_POLL_INTERVAL_MS) {
    return;
  }
  Last_Sensor_Poll_Ms = millis();

  uint16_t level_tmp = 0;
  int16_t temp_tmp = 0;
  if (Next_Sensor_ID == SENSOR_ID_1) {
    const bool ok = Read_Sensor_WithRetry(SENSOR_ID_1, &level_tmp, &temp_tmp);
    if (ok) {
      const uint32_t now = millis();
      Sensor_Level_mm_1 = level_tmp;
      Sensor_Temp_x10_1 = temp_tmp;
      Sensor_HasValue_1 = true;
      Sensor_HasTemp_1 = true;
      Sensor_Last_Ok_Ms_1 = now;

      if (Sensor_Prev_Valid_1 && Sensor_Prev_Level_Ms_1 > 0) {
        const uint32_t dt = now - Sensor_Prev_Level_Ms_1;
        if (dt > 0) {
          const int32_t jump = (int32_t)Sensor_Level_mm_1 - (int32_t)Sensor_Prev_Level_mm_1;
          const uint32_t jump_rate = (uint32_t)(abs(jump) * 1000UL / dt);
          if (jump_rate > LEVEL_JUMP_THRESHOLD_MM_PER_S) {
            Alarm_LevelJump_ExpireMs = now + 15000UL;
          }
        }
      }
      if (Sensor_Level_mm_1 < LEVEL_MIN_MM || Sensor_Level_mm_1 > LEVEL_MAX_MM) {
        Alarm_LevelRange_ExpireMs = now + 15000UL;
      }
      Sensor_Prev_Level_mm_1 = Sensor_Level_mm_1;
      Sensor_Prev_Level_Ms_1 = now;
      Sensor_Prev_Valid_1 = true;
    }
    Next_Sensor_ID = SENSOR_ID_2;
  } else {
    const bool ok = Read_Sensor_WithRetry(SENSOR_ID_2, &level_tmp, &temp_tmp);
    if (ok) {
      const uint32_t now = millis();
      Sensor_Level_mm_2 = level_tmp;
      Sensor_Temp_x10_2 = temp_tmp;
      Sensor_HasValue_2 = true;
      Sensor_HasTemp_2 = true;
      Sensor_Last_Ok_Ms_2 = now;

      if (Sensor_Prev_Valid_2 && Sensor_Prev_Level_Ms_2 > 0) {
        const uint32_t dt = now - Sensor_Prev_Level_Ms_2;
        if (dt > 0) {
          const int32_t jump = (int32_t)Sensor_Level_mm_2 - (int32_t)Sensor_Prev_Level_mm_2;
          const uint32_t jump_rate = (uint32_t)(abs(jump) * 1000UL / dt);
          if (jump_rate > LEVEL_JUMP_THRESHOLD_MM_PER_S) {
            Alarm_LevelJump_ExpireMs = now + 15000UL;
          }
        }
      }
      if (Sensor_Level_mm_2 < LEVEL_MIN_MM || Sensor_Level_mm_2 > LEVEL_MAX_MM) {
        Alarm_LevelRange_ExpireMs = now + 15000UL;
      }
      Sensor_Prev_Level_mm_2 = Sensor_Level_mm_2;
      Sensor_Prev_Level_Ms_2 = now;
      Sensor_Prev_Valid_2 = true;
    }
    Next_Sensor_ID = SENSOR_ID_1;
  }



  {
    const uint32_t now = millis();
    Sensor_Online_1 = (Sensor_Last_Ok_Ms_1 > 0) && ((now - Sensor_Last_Ok_Ms_1) <= (uint32_t)SENSOR_ONLINE_GRACE_MS);
    Sensor_Online_2 = (Sensor_Last_Ok_Ms_2 > 0) && ((now - Sensor_Last_Ok_Ms_2) <= (uint32_t)SENSOR_ONLINE_GRACE_MS);
  }

  if (SERIAL_LEVEL_LOG_Enable && (millis() - Last_Level_Log_Ms >= SERIAL_LEVEL_LOG_INTERVAL_MS)) {
    Last_Level_Log_Ms = millis();
    printf("[Level] ID001(inner):");
    if (Sensor_HasValue_1) {
      printf("%umm(%.3fm)", Sensor_Level_mm_1, Sensor_Level_mm_1 / 1000.0f);
    } else {
      printf("offline");
    }
    printf(" | ID002(outer):");
    if (Sensor_HasValue_2) {
      printf("%umm(%.3fm)", Sensor_Level_mm_2, Sensor_Level_mm_2 / 1000.0f);
    } else {
      printf("offline");
    }
    printf(" | T1:");
    if (Sensor_HasTemp_1) {
      printf("%.1fC", Sensor_Temp_x10_1 / 10.0f);
    } else {
      printf("offline");
    }
    printf(" | T2:");
    if (Sensor_HasTemp_2) {
      printf("%.1fC", Sensor_Temp_x10_2 / 10.0f);
    } else {
      printf("offline");
    }
    if (!Sensor_Online_1 || !Sensor_Online_2) {
      printf(" [online:%d,%d]", Sensor_Online_1 ? 1 : 0, Sensor_Online_2 ? 1 : 0);
    }
    printf("\r\n");
  }

  if ((millis() - Log_LastMeasureMs) >= LOG_MEASURE_INTERVAL_MS) {
    Log_LastMeasureMs = millis();
    WS_Log_Measure("inner_mm=%u outer_mm=%u t1_x10=%d t2_x10=%d online1=%d online2=%d valid1=%d valid2=%d",
                   (unsigned)Sensor_Level_mm_1,
                   (unsigned)Sensor_Level_mm_2,
                   (int)Sensor_Temp_x10_1,
                   (int)Sensor_Temp_x10_2,
                   Sensor_Online_1 ? 1 : 0,
                   Sensor_Online_2 ? 1 : 0,
                   Sensor_HasValue_1 ? 1 : 0,
                   Sensor_HasValue_2 ? 1 : 0);
  }
}




void Relay_Analysis(uint8_t *buf,uint8_t Mode_Flag)
{
  const bool isGateCommand = (buf[0] == GATE_STOP || buf[0] == CH1 || buf[0] == CH2);
  if (!isGateCommand) {
    if (Mode_Flag == MQTT_Mode) {
      printf("WIFI Data :");
    } else {
      printf("RS485 Data :");
    }
  }
  switch(buf[0])
  {
    case GATE_STOP:
      Pause_Auto_By_ManualTakeover();
      Gate_Stop();
      Buzzer_PWM(60);
      Gate_Log("|***  Gate STOP (Relay CH1/CH2 off) ***|\r\n");
      break;
    case CH1:
      Pause_Auto_By_ManualTakeover();
      if (!Gate_Open()) {
        if (Gate_Should_Log_Block()) {
          Gate_Log("|***  Gate OPEN blocked: %s ***|\r\n", Gate_Block_Reason);
        }
        break;
      }
      Buzzer_PWM(100);
      Gate_Log("|***  Gate OPEN (Relay CH1) ***|\r\n");
      break;
    case CH2:
      Pause_Auto_By_ManualTakeover();
      if (!Gate_Close()) {
        if (Gate_Should_Log_Block()) {
          Gate_Log("|***  Gate CLOSE blocked: %s ***|\r\n", Gate_Block_Reason);
        }
        break;
      }
      Buzzer_PWM(100);
      Gate_Log("|***  Gate CLOSE (Relay CH2) ***|\r\n");
      break;
    case CH3:
      digitalToggle(GPIO_PIN_CH3);
      Relay_Flag[2] =! Relay_Flag[2];
      Buzzer_PWM(100);
      if(Relay_Flag[2])
        printf("|***  Relay CH3 on  ***|\r\n");
      else
        printf("|***  Relay CH3 off ***|\r\n");
      break;
    case CH4:
      digitalToggle(GPIO_PIN_CH4);
      Relay_Flag[3] =! Relay_Flag[3];
      Buzzer_PWM(100);
      if(Relay_Flag[3])
        printf("|***  Relay CH4 on  ***|\r\n");
      else
        printf("|***  Relay CH4 off ***|\r\n");
      break;
    case CH5:
      digitalToggle(GPIO_PIN_CH5);
      Relay_Flag[4] =! Relay_Flag[4];
      Buzzer_PWM(100);
      if(Relay_Flag[4])
        printf("|***  Relay CH5 on  ***|\r\n");
      else
        printf("|***  Relay CH5 off ***|\r\n");
      break;
    case CH6:
      digitalToggle(GPIO_PIN_CH6);
      Relay_Flag[5] =! Relay_Flag[5];
      Buzzer_PWM(100);
      if(Relay_Flag[5])
        printf("|***  Relay CH6 on  ***|\r\n");
      else
        printf("|***  Relay CH6 off ***|\r\n");
      break;
    case ALL_ON:

      if (Gate_Action_Active || Relay_Flag[0] || Relay_Flag[1]) {
        Gate_Stop();
      }
      digitalWrite(GPIO_PIN_CH1, LOW);
      digitalWrite(GPIO_PIN_CH2, LOW);
      digitalWrite(GPIO_PIN_CH3, HIGH);
      digitalWrite(GPIO_PIN_CH4, HIGH);
      digitalWrite(GPIO_PIN_CH5, HIGH);
      digitalWrite(GPIO_PIN_CH6, HIGH);
      Relay_Flag[0] = 0;
      Relay_Flag[1] = 0;
      Relay_Flag[2] = 1;
      Relay_Flag[3] = 1;
      Relay_Flag[4] = 1;
      Relay_Flag[5] = 1;
      Gate_State = GATE_STATE_STOPPED;
      Update_Gate_Command_Availability();
      printf("|***  Relay ALL on (CH3-CH6), Gate relays kept OFF ***|\r\n");
      Buzzer_PWM(300);
      break;
    case ALL_OFF:
      Gate_Stop();
      digitalWrite(GPIO_PIN_CH3, LOW);
      digitalWrite(GPIO_PIN_CH4, LOW);
      digitalWrite(GPIO_PIN_CH5, LOW);
      digitalWrite(GPIO_PIN_CH6, LOW);
      memset(Relay_Flag,0, sizeof(Relay_Flag));
      Update_Gate_Command_Availability();
      printf("|***  Relay ALL off ***|\r\n");
      Buzzer_PWM(100);
      delay(100);
      Buzzer_PWM(100);
      break;
    default:
      printf("Note : Non-instruction data was received !\r\n");
  }
}



static void Offline_Network_RGB_Loop()
{
  if (!WIFI_OFFLINE_RGB_BLINK_Enable) {
    return;
  }

  static uint32_t lastBlinkMs = 0;
  static bool redPhase = true;
  static bool blinking = false;

  const bool wifiOnline = WIFI_Connection && (WiFi.status() == WL_CONNECTED);
  if (wifiOnline) {
    if (blinking) {
      RGB_Light(0, 0, 0);
      blinking = false;
    }
    return;
  }

  if (millis() - lastBlinkMs < (uint32_t)WIFI_OFFLINE_RGB_BLINK_INTERVAL_MS) {
    return;
  }
  lastBlinkMs = millis();
  redPhase = !redPhase;
  blinking = true;

  if (redPhase) {
    RGB_Light(80, 0, 0);
  } else {
    RGB_Light(0, 0, 80);
  }
}

void setup() {

  Serial_Init();
  WS_Log_Init();
  WS_Log_SetTimeProvider(WS_Time_NowEpoch);

  GPIO_Init();
  Buzzer_Startup_Melody(STARTUP_BUZZER_DURATION_MS);
  Update_Gate_Command_Availability();
  Update_Alarm_Status();
  Alarm_LogTransitions();
  Ctrl_LoadIfNeeded();


  MQTT_Init();
  if (WIFI_Connection == 1 && WiFi.status() == WL_CONNECTED) {
    WS_Time_OnWiFiConnected();
  }
}


void loop() {

  Sensor_Read_Loop();
  Manual_Takeover_Loop();
  WS_Time_Loop();
  Ctrl_Automation_Loop();
  Gate_Action_Loop();


  const bool ch1On = Relay_Flag[0] || (digitalRead(GPIO_PIN_CH1) == HIGH);
  const bool ch2On = Relay_Flag[1] || (digitalRead(GPIO_PIN_CH2) == HIGH);
  if (ch1On && ch2On) {
    Alarm_RelayInterlock = true;
    Gate_Stop();
  }
  Update_Gate_Command_Availability();
  Update_Alarm_Status();
  Alarm_LogTransitions();


  MQTT_Loop();
  Air780E_Loop();
  Offline_Network_RGB_Loop();
}