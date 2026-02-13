#include "WS_MQTT.h"
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ElegantOTA.h>
#include <cstring>
#include <ArduinoJson.h>

#include "WS_Control.h"
#include "WS_Log.h"

// The name and password of the WiFi access point
const char* ssid = STASSID;
const char* password = STAPSK;
// MQTT broker config
const char* mqtt_server = MQTT_Server;
int PORT = MQTT_Port;
const char* ID = MQTT_ID;   // Defining device ID
const char* mqtt_user = MQTT_Username;
const char* mqtt_password = MQTT_Password;
const char* pub = MQTT_Pub; // MQTT publish topic
const char* sub = MQTT_Sub; // MQTT subscribe topic


WiFiClient espClient;                 // MQTT initializes the contents
PubSubClient client(espClient);
WebServer server(80);                 // Declare the WebServer object


bool WIFI_Connection = 0;
char ipStr[16];

extern uint16_t Sensor_Level_mm_1;
extern uint16_t Sensor_Level_mm_2;
extern int16_t Sensor_Temp_x10_1;
extern int16_t Sensor_Temp_x10_2;
extern bool Sensor_HasValue_1;
extern bool Sensor_HasValue_2;
extern bool Sensor_HasTemp_1;
extern bool Sensor_HasTemp_2;
extern bool Sensor_Online_1;
extern bool Sensor_Online_2;
extern uint8_t Gate_State;
extern bool Gate_Position_Open;
extern bool Gate_AutoControl_Enabled;
extern bool Gate_Auto_Latched_Off;
extern bool Gate_Open_Allowed;
extern bool Gate_Close_Allowed;
extern char Gate_Block_Reason[96];
extern bool Manual_Takeover_Active;
extern uint32_t Manual_Takeover_UntilMs;
extern bool Alarm_Active;
extern uint8_t Alarm_Severity;
extern char Alarm_Text[128];
extern bool Air780E_Online;
extern bool Air780E_SIMReady;
extern bool Air780E_Attached;
extern int Air780E_CSQ;
extern int Air780E_RSSI_dBm;
extern uint32_t Air780E_LastRxMs;
extern void End_Manual_Takeover();
extern void Enable_Auto_Mode();
extern void Latch_Auto_Off();
extern void Pause_Auto_By_ManualTakeover();
static char OtaLatestVersion[32] = "ElegantOTA";
static char OtaLastCheck[24] = "n/a";
static char OtaLastResult[96] = "web_update_only";
static bool Mqtt_State_Dirty = true;
static uint32_t Mqtt_LastPublishMs = 0;

static bool MQTT_HasAuth()
{
  return mqtt_user != nullptr && mqtt_user[0] != '\0';
}

static bool Http_Auth()
{
  if (!HTTP_AUTH_Enable) {
    return true;
  }
  if (server.authenticate(HTTP_AUTH_Username, HTTP_AUTH_Password)) {
    return true;
  }
  server.requestAuthentication();
  return false;
}

static void Api_SendJson(int code, const char* json)
{
  if (json == nullptr) {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"null\"}");
    return;
  }
  server.send(code, "application/json", json);
}

static bool ParseCmdFromJsonBody(String& outCmd)
{
  outCmd = "";
  if (!server.hasArg("plain")) {
    return false;
  }
  const String body = server.arg("plain");
  if (body.length() == 0) {
    return false;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    return false;
  }
  const char* cmd = doc["cmd"];
  if (cmd == nullptr) {
    return false;
  }
  outCmd = String(cmd);
  outCmd.trim();
  return outCmd.length() > 0;
}

static bool HandleCmdString(const String& cmd)
{
  String c = cmd;
  c.toLowerCase();
  if (c == "gate_open") { uint8_t Data[1] = {'1'}; Relay_Analysis(Data, WIFI_Mode); return true; }
  if (c == "gate_close") { uint8_t Data[1] = {'2'}; Relay_Analysis(Data, WIFI_Mode); return true; }
  if (c == "gate_stop") { uint8_t Data[1] = {'0'}; Relay_Analysis(Data, WIFI_Mode); return true; }
  if (c == "auto_on") { Enable_Auto_Mode(); return true; }
  if (c == "auto_off") { Pause_Auto_By_ManualTakeover(); return true; }
  if (c == "auto_latch_off") { Latch_Auto_Off(); return true; }
  if (c == "manual_end") { End_Manual_Takeover(); return true; }
  return false;
}

static void Ota_SetStatus(const char* latest, const char* result)
{
  if (latest && latest[0] != '\0') {
    snprintf(OtaLatestVersion, sizeof(OtaLatestVersion), "%s", latest);
  }
  if (result && result[0] != '\0') {
    snprintf(OtaLastResult, sizeof(OtaLastResult), "%s", result);
  }
  snprintf(OtaLastCheck, sizeof(OtaLastCheck), "%lus", (unsigned long)(millis() / 1000UL));
}

static void MQTT_BuildStateJson(char* json, size_t jsonSize)
{
  const int rssi = WIFI_Connection ? WiFi.RSSI() : -127;
  const bool mqttConnected = MQTT_CLOUD_Enable ? client.connected() : false;
  const uint32_t nowMs = millis();
  uint32_t manualRemainS = 0;
  if (Manual_Takeover_Active && Manual_Takeover_UntilMs > nowMs) {
    manualRemainS = (Manual_Takeover_UntilMs - nowMs + 999UL) / 1000UL;
  }
  uint32_t airLastRxAgeS = 0;
  if (Air780E_LastRxMs > 0 && nowMs >= Air780E_LastRxMs) {
    airLastRxAgeS = (nowMs - Air780E_LastRxMs) / 1000UL;
  }

  snprintf(
    json,
    jsonSize,
    "{\"sensor1\":{\"mm\":%u,\"valid\":%s,\"online\":%s,\"temp_x10\":%d,\"temp_valid\":%s},\"sensor2\":{\"mm\":%u,\"valid\":%s,\"online\":%s,\"temp_x10\":%d,\"temp_valid\":%s},\"gate_state\":%u,\"gate_position_open\":%s,\"auto_gate\":%s,\"auto_latched\":%s,\"manual\":{\"active\":%s,\"remain_s\":%lu},\"relay1\":%u,\"relay2\":%u,\"net\":{\"wifi\":%s,\"mqtt\":%s,\"http\":%s,\"ip\":\"%s\",\"rssi\":%d},\"cell\":{\"enabled\":%s,\"online\":%s,\"sim_ready\":%s,\"attached\":%s,\"csq\":%d,\"rssi_dbm\":%d,\"last_rx_age_s\":%lu},\"ctrl\":{\"open_allowed\":%s,\"close_allowed\":%s,\"reason\":\"%s\"},\"alarm\":{\"active\":%s,\"severity\":%u,\"text\":\"%s\"},\"fw\":{\"current\":\"%s\",\"latest\":\"%s\",\"last_check\":\"%s\",\"last_result\":\"%s\"}}",
    Sensor_Level_mm_1,
    Sensor_HasValue_1 ? "true" : "false",
    Sensor_Online_1 ? "true" : "false",
    Sensor_Temp_x10_1,
    Sensor_HasTemp_1 ? "true" : "false",
    Sensor_Level_mm_2,
    Sensor_HasValue_2 ? "true" : "false",
    Sensor_Online_2 ? "true" : "false",
    Sensor_Temp_x10_2,
    Sensor_HasTemp_2 ? "true" : "false",
    Gate_State,
    Gate_Position_Open ? "true" : "false",
    Gate_AutoControl_Enabled ? "true" : "false",
    Gate_Auto_Latched_Off ? "true" : "false",
    Manual_Takeover_Active ? "true" : "false",
    (unsigned long)manualRemainS,
    Relay_Flag[0] ? 1 : 0,
    Relay_Flag[1] ? 1 : 0,
    WIFI_Connection ? "true" : "false",
    mqttConnected ? "true" : "false",
    WIFI_Connection ? "true" : "false",
    ipStr,
    rssi,
    AIR780E_Enable ? "true" : "false",
    Air780E_Online ? "true" : "false",
    Air780E_SIMReady ? "true" : "false",
    Air780E_Attached ? "true" : "false",
    Air780E_CSQ,
    Air780E_RSSI_dBm,
    (unsigned long)airLastRxAgeS,
    Gate_Open_Allowed ? "true" : "false",
    Gate_Close_Allowed ? "true" : "false",
    Gate_Block_Reason,
    Alarm_Active ? "true" : "false",
    Alarm_Severity,
    Alarm_Text,
    FW_VERSION,
    OtaLatestVersion,
    OtaLastCheck,
    OtaLastResult
  );
}

static void MQTT_MarkStateDirty()
{
  Mqtt_State_Dirty = true;
}

static void MQTT_PublishState(bool force)
{
  if (!MQTT_CLOUD_Enable || !client.connected()) {
    return;
  }

  const uint32_t nowMs = millis();
  const bool intervalDue = (nowMs - Mqtt_LastPublishMs) >= (uint32_t)MQTT_TELEMETRY_INTERVAL_MS;
  const bool changeDue = MQTT_PUBLISH_ON_CHANGE_Enable && Mqtt_State_Dirty;
  if (!force && !intervalDue && !changeDue) {
    return;
  }

  char json[1700];
  MQTT_BuildStateJson(json, sizeof(json));
  if (client.publish(pub, json, false)) {
    Mqtt_LastPublishMs = nowMs;
    Mqtt_State_Dirty = false;
  }
}

void handleRoot() {
  if (!Http_Auth()) {
    return;
  }
  // Unified minimal panel style (same as VPS panel), internal panel has no login.
  String myhtmlPage = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Fish 控制面板</title>
  <style>
    :root{--bg:#f6f7fb;--card:#fff;--line:#e5e7eb;--text:#0f172a;--muted:#475569;--accent:#0ea5e9;--warn:#d97706;--bad:#dc2626}
    *{box-sizing:border-box}
    body{margin:0;font-family:ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,"PingFang SC","Microsoft YaHei",sans-serif;background:var(--bg);color:var(--text);padding:18px}
    .top{display:flex;align-items:center;justify-content:space-between;gap:14px;max-width:980px;margin:0 auto 14px;padding:12px 14px;border:1px solid var(--line);border-radius:14px;background:var(--card)}
    .top-left{display:flex;align-items:center;gap:12px}
    .dot{width:10px;height:10px;border-radius:50%;background:var(--accent);box-shadow:0 0 0 4px rgba(14,165,233,.15)}
    .title{font-weight:900;letter-spacing:.2px}
    .sub{color:var(--muted);font-size:12px;margin-top:2px}
    .grid{max-width:980px;margin:0 auto;display:grid;grid-template-columns:1fr 1fr;gap:12px}
    .card{border:1px solid var(--line);border-radius:14px;background:var(--card);padding:14px;box-shadow:0 12px 30px rgba(15,23,42,.08)}
    .card-title{font-size:13px;color:var(--muted);font-weight:800;margin-bottom:10px;letter-spacing:.2px}
    .kv{display:grid;grid-template-columns:90px 1fr;gap:8px 10px;align-items:center}
    .k{color:var(--muted);font-size:12px;font-weight:800}
    .v{font-size:20px;font-weight:900}
    .row{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
    .pill{border:1px solid var(--line);border-radius:999px;padding:6px 10px;font-size:12px;color:var(--muted);background:#f8fafc}
    .controls{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin-top:12px}
    .btn{appearance:none;border:1px solid var(--line);background:#fff;color:var(--text);border-radius:12px;padding:10px 12px;font-weight:900;cursor:pointer;min-height:44px}
    .btn:hover{background:#f8fafc}
    .btn:active{transform:translateY(1px)}
    .btn.primary{border-color:#0284c7;background:var(--accent);color:#fff}
    .btn.danger{border-color:#dc2626;background:#fef2f2;color:#7f1d1d}
    .btn.warn{border-color:#d97706;background:#fff7ed;color:#7c2d12}
    .hint{margin-top:10px;color:var(--muted);font-size:12px}
    .alarm{padding:10px 12px;border-radius:12px;border:1px solid var(--line);background:#f8fafc;font-weight:900}
    .alarm-on{border-color:#fecaca;background:#fef2f2}
    @media (max-width:900px){.grid{grid-template-columns:1fr}}
    @media (max-width:520px){.controls{grid-template-columns:1fr 1fr}}
  </style>
</head>
<body>
  <header class="top">
    <div class="top-left">
      <div class="dot"></div>
      <div>
        <div class="title">Fish 控制面板</div>
        <div class="sub" id="statusLine">连接中…</div>
      </div>
    </div>
    <a class="btn" href="/config" style="text-decoration:none;display:inline-grid;place-items:center">配置</a>
  </header>

  <main class="grid">
    <section class="card">
      <div class="card-title">水位</div>
      <div class="kv">
        <div class="k">内塘</div><div class="v" id="innerLevel">--</div>
        <div class="k">外塘</div><div class="v" id="outerLevel">--</div>
        <div class="k">水位差</div><div class="v" id="delta">--</div>
      </div>
      <div class="row">
        <div class="pill" id="innerOnline">内塘：--</div>
        <div class="pill" id="outerOnline">外塘：--</div>
      </div>
    </section>

    <section class="card">
      <div class="card-title">闸门</div>
      <div class="kv">
        <div class="k">状态</div><div class="v" id="gateState">--</div>
        <div class="k">自动</div><div class="v" id="autoState">--</div>
      </div>
      <div class="controls">
        <button class="btn primary" onclick="cmd('gate_open')">开闸</button>
        <button class="btn primary" onclick="cmd('gate_close')">关闸</button>
        <button class="btn danger" onclick="cmd('gate_stop')">停止</button>
        <button class="btn" onclick="cmd('auto_on')">开启自动</button>
        <button class="btn" onclick="cmd('auto_off')">手动接管(暂停)</button>
        <button class="btn" onclick="cmd('manual_end')">恢复自动</button>
        <button class="btn warn" onclick="confirmLatch()">关闭自动(锁定)</button>
      </div>
      <div class="hint" id="interlock">互锁：--</div>
    </section>

    <section class="card">
      <div class="card-title">告警</div>
      <div class="alarm" id="alarmLine">--</div>
      <div class="hint" id="metaLine">FW __FW_VERSION__</div>
    </section>
  </main>

  <script>
    const $ = (id) => document.getElementById(id);
    function setStatus(t){ $('statusLine').textContent=t; }
    function gateStateText(s){ if(s===1) return '开闸中'; if(s===2) return '关闸中'; return '待机'; }
    function render(t){
      if(!t){ return; }
      const s1=t.sensor1||{}, s2=t.sensor2||{};
      $('innerLevel').textContent = s1.valid ? (s1.mm+' mm') : '离线';
      $('outerLevel').textContent = s2.valid ? (s2.mm+' mm') : '离线';
      $('delta').textContent = (s1.valid && s2.valid) ? ((s1.mm-s2.mm)+' mm') : '--';
      $('innerOnline').textContent = '内塘：'+(s1.online?'在线':'离线');
      $('outerOnline').textContent = '外塘：'+(s2.online?'在线':'离线');
      $('gateState').textContent = gateStateText(t.gate_state);
      $('autoState').textContent = t.auto_latched ? '锁定关闭' : (t.auto_gate?'已启用':'已关闭');
      const reason=(t.ctrl&&t.ctrl.reason)?t.ctrl.reason:'';
      $('interlock').textContent='互锁：'+(reason||'正常');
      if(t.alarm && t.alarm.active){
        $('alarmLine').textContent = t.alarm.text || '告警';
        $('alarmLine').className='alarm alarm-on';
      }else{
        $('alarmLine').textContent = 'normal';
        $('alarmLine').className='alarm';
      }
      if(t.net){
        setStatus(`Wi-Fi ${t.net.wifi?'在线':'离线'} | MQTT ${t.net.mqtt?'在线':'离线'} | IP ${t.net.ip||'--'}`);
      }
    }
    async function cmd(c){
      try{
        await fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({cmd:c})});
      }catch(e){}
      refresh();
    }
    function confirmLatch(){ if(confirm('确认关闭自动(锁定)？')) cmd('auto_latch_off'); }
    async function refresh(){
      try{
        const s = await (await fetch('/api/state')).json();
        render(s.telemetry);
      }catch(e){
        setStatus('刷新失败');
      }
    }
    setInterval(refresh, 1000);
    refresh();
  </script>
</body>
</html>
)HTML";
  myhtmlPage.replace("__FW_VERSION__", FW_VERSION);
  String otaPage = String("http://") + ipStr + String("/update");
  myhtmlPage.replace("__FW_OTA_PAGE__", otaPage);

  server.send(200, "text/html", myhtmlPage);
  printf("The user visited the home page\r\n");
}
void handleGetData() {
  if (!Http_Auth()) {
    return;
  }
  char json[1700];
  MQTT_BuildStateJson(json, sizeof(json));
  server.send(200, "application/json", json);
}

void handleApiState()
{
  if (!Http_Auth()) {
    return;
  }
  char tele[1700];
  MQTT_BuildStateJson(tele, sizeof(tele));
  char out[2000];
  const bool mqttConnected = MQTT_CLOUD_Enable ? client.connected() : false;
  snprintf(out, sizeof(out),
           "{\"mqtt_connected\":%s,\"last_telemetry_at\":%lu,\"telemetry\":%s}",
           mqttConnected ? "true" : "false",
           (unsigned long)millis(),
           tele);
  Api_SendJson(200, out);
}

void handleApiCmd()
{
  if (!Http_Auth()) {
    return;
  }
  String cmd;
  bool ok = false;
  if (ParseCmdFromJsonBody(cmd)) {
    ok = HandleCmdString(cmd);
  } else if (server.hasArg("cmd")) {
    cmd = server.arg("cmd");
    ok = HandleCmdString(cmd);
  }
  if (ok) {
    MQTT_MarkStateDirty();
    Api_SendJson(200, "{\"ok\":true}");
    return;
  }
  Api_SendJson(400, "{\"ok\":false,\"error\":\"bad_cmd\"}");
}

void handleConfigPage()
{
  if (!Http_Auth()) {
    return;
  }
  const char* page = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>配置</title>
  <style>
    :root{--bg:#f6f7fb;--card:#fff;--line:#e5e7eb;--text:#0f172a;--muted:#475569;--accent:#0ea5e9}
    *{box-sizing:border-box}
    body{margin:0;font-family:ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,"PingFang SC","Microsoft YaHei",sans-serif;background:var(--bg);color:var(--text);padding:18px}
    .wrap{max-width:980px;margin:0 auto}
    .top{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:12px}
    .btn{appearance:none;border:1px solid var(--line);background:#fff;color:var(--text);border-radius:12px;padding:10px 12px;font-weight:900;cursor:pointer;min-height:44px;text-decoration:none;display:inline-grid;place-items:center}
    .btn.primary{border-color:#0284c7;background:var(--accent);color:#fff}
    .card{border:1px solid var(--line);border-radius:14px;background:var(--card);padding:14px;box-shadow:0 12px 30px rgba(15,23,42,.08)}
    textarea{width:100%;min-height:420px;border:1px solid var(--line);border-radius:12px;padding:10px 12px;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:13px}
    .hint{margin-top:10px;color:var(--muted);font-size:12px;line-height:1.6}
  </style>
</head>
<body>
  <div class="wrap">
    <div class="top">
      <a class="btn" href="/">返回面板</a>
      <button class="btn primary" onclick="save()">保存</button>
    </div>
    <div class="card">
      <div style="font-weight:900;margin-bottom:8px">控制配置（存储在 ESP32 LittleFS: /ctrl.json）</div>
      <textarea id="cfg"></textarea>
      <div class="hint">
        mode: mixed/daily/cycle/leveldiff。daily 支持多组 open/close 时间；cycle 支持 steps 多段；leveldiff 按水位差控制。
      </div>
      <div class="hint" id="msg"></div>
    </div>
  </div>
  <script>
    async function load(){
      const t = await (await fetch('/api/config')).text();
      document.getElementById('cfg').value = t;
    }
    async function save(){
      const body = document.getElementById('cfg').value;
      const r = await fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'}, body});
      document.getElementById('msg').textContent = r.ok ? '已保存' : ('保存失败: '+(await r.text()));
    }
    load();
  </script>
</body>
</html>
)HTML";
  server.send(200, "text/html", page);
}

void handleApiConfigGet()
{
  if (!Http_Auth()) {
    return;
  }
  const String raw = WS_Control_LoadRawJson();
  if (raw.length() > 0) {
    server.send(200, "application/json", raw);
    return;
  }
  WS_ControlConfig cfg;
  (void)WS_Control_Load(cfg);
  const String fallback = WS_Control_LoadRawJson();
  server.send(200, "application/json", fallback);
}

void handleApiConfigPost()
{
  if (!Http_Auth()) {
    return;
  }
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "missing body");
    return;
  }
  const String body = server.arg("plain");
  if (!WS_Control_SaveRawJson(body.c_str())) {
    server.send(400, "text/plain", "invalid json");
    return;
  }
  // Mark telemetry dirty so MQTT can publish updated state quickly.
  MQTT_MarkStateDirty();
  server.send(200, "application/json", "{\"ok\":true}");
}
void handleSwitch(int ledNumber) {
  if (!Http_Auth()) {
    return;
  }
  uint8_t Data[1]={0};
  Data[0] = static_cast<uint8_t>(ledNumber + 48);
  Relay_Analysis(Data,WIFI_Mode);
  MQTT_MarkStateDirty();
  server.send(200, "text/plain", "OK");
}
void handleSwitch1() { handleSwitch(1); }
void handleSwitch2() { handleSwitch(2); }
void handleSwitch3() { handleSwitch(3); }
void handleSwitch4() { handleSwitch(4); }
void handleSwitch5() { handleSwitch(5); }
void handleSwitch6() { handleSwitch(6); }
void handleSwitch7() { handleSwitch(7); }
void handleSwitch8() { handleSwitch(8); }
void handleGateOpen() { if (!Http_Auth()) return; uint8_t Data[1] = {'1'}; Relay_Analysis(Data, WIFI_Mode); MQTT_MarkStateDirty(); server.send(200, "text/plain", "OK"); }
void handleGateClose() { if (!Http_Auth()) return; uint8_t Data[1] = {'2'}; Relay_Analysis(Data, WIFI_Mode); MQTT_MarkStateDirty(); server.send(200, "text/plain", "OK"); }
void handleGateStop() { if (!Http_Auth()) return; uint8_t Data[1] = {'0'}; Relay_Analysis(Data, WIFI_Mode); MQTT_MarkStateDirty(); server.send(200, "text/plain", "OK"); }
void handleAutoGateOn() { if (!Http_Auth()) return; Enable_Auto_Mode(); MQTT_MarkStateDirty(); server.send(200, "text/plain", "OK"); }
void handleAutoGateOff() { if (!Http_Auth()) return; Pause_Auto_By_ManualTakeover(); MQTT_MarkStateDirty(); server.send(200, "text/plain", "OK"); }
void handleAutoGateLatchOff() { if (!Http_Auth()) return; Latch_Auto_Off(); MQTT_MarkStateDirty(); server.send(200, "text/plain", "OK"); }
void handleManualEnd() { if (!Http_Auth()) return; End_Manual_Takeover(); MQTT_MarkStateDirty(); server.send(200, "text/plain", "OK"); }
/************************************************** MQTT *********************************************/
// MQTT subscribes to callback functions for processing received messages
void callback(char* topic, byte* payload, unsigned int length) {
  (void)topic;
  uint8_t CH_Flag = 0;
  bool commandHandled = false;
  String inputString;
  for (int i = 0; i < (int)length; i++) {
    inputString += (char)payload[i];
  }
  printf("%s\r\n",inputString.c_str());                            // Supported formats: {"data":{"CH1":1}} / {"cmd":"gate_open"}

  String lowerInput = inputString;
  lowerInput.toLowerCase();
  if (lowerInput.indexOf("\"cmd\"") != -1) {
    if (lowerInput.indexOf("gate_open") != -1) {
      uint8_t Data[1] = {'1'};
      Relay_Analysis(Data, MQTT_Mode);
      commandHandled = true;
    } else if (lowerInput.indexOf("gate_close") != -1) {
      uint8_t Data[1] = {'2'};
      Relay_Analysis(Data, MQTT_Mode);
      commandHandled = true;
    } else if (lowerInput.indexOf("gate_stop") != -1) {
      uint8_t Data[1] = {'0'};
      Relay_Analysis(Data, MQTT_Mode);
      commandHandled = true;
    } else if (lowerInput.indexOf("auto_on") != -1) {
      Enable_Auto_Mode();
      commandHandled = true;
    } else if (lowerInput.indexOf("auto_off") != -1) {
      Pause_Auto_By_ManualTakeover();
      commandHandled = true;
    } else if (lowerInput.indexOf("auto_latch_off") != -1) {
      Latch_Auto_Off();
      commandHandled = true;
    } else if (lowerInput.indexOf("manual_end") != -1) {
      End_Manual_Takeover();
      commandHandled = true;
    }
  }

  const int dataBegin = inputString.indexOf("\"data\"");                 // Find if "data" exists in the string (quotes also)
  if (dataBegin != -1) {
    int CH_Begin = -1;
    if (inputString.indexOf("\"CH1\"", dataBegin) != -1){             // Find if "CH1" is present in the string (quotes also)
      CH_Flag = 1;
      CH_Begin = inputString.indexOf("\"CH1\"", dataBegin);
    }
    else if (inputString.indexOf("\"CH2\"", dataBegin) != -1){
      CH_Flag = 2;
      CH_Begin = inputString.indexOf("\"CH2\"", dataBegin);
    }
    else if (inputString.indexOf("\"CH3\"", dataBegin) != -1){
      CH_Flag = 3;
      CH_Begin = inputString.indexOf("\"CH3\"", dataBegin);
    }
    else if (inputString.indexOf("\"CH4\"", dataBegin) != -1){
      CH_Flag = 4;
      CH_Begin = inputString.indexOf("\"CH4\"", dataBegin);
    }
    else if (inputString.indexOf("\"CH5\"", dataBegin) != -1){
      CH_Flag = 5;
      CH_Begin = inputString.indexOf("\"CH5\"", dataBegin);
    }
    else if (inputString.indexOf("\"CH6\"", dataBegin) != -1){
      CH_Flag = 6;
      CH_Begin = inputString.indexOf("\"CH6\"", dataBegin);
    }
    else if (inputString.indexOf("\"ALL\"", dataBegin) != -1){
      CH_Flag = 7;
      CH_Begin = inputString.indexOf("\"ALL\"", dataBegin);
    }

    int valueBegin = -1;
    if (CH_Begin != -1) {
      valueBegin = inputString.indexOf(':', CH_Begin);
    }
    int valueEnd = -1;
    if (valueBegin != -1) {
      const int valueEndComma = inputString.indexOf(',', valueBegin + 1);
      const int valueEndBrace = inputString.indexOf('}', valueBegin + 1);
      if (valueEndComma == -1) {
        valueEnd = valueEndBrace;
      } else if (valueEndBrace == -1) {
        valueEnd = valueEndComma;
      } else {
        valueEnd = (valueEndComma < valueEndBrace) ? valueEndComma : valueEndBrace;
      }
    }

    if (CH_Flag != 0 && valueBegin != -1 && valueEnd != -1) {
      String ValueStr = inputString.substring(valueBegin + 1, valueEnd);
      ValueStr.trim();
      const int Value = ValueStr.toInt();
      if(CH_Flag < 7){
        if (CH_Flag == 1 || CH_Flag == 2) {
          if (Value == 1) {
            uint8_t Data[1] = {static_cast<uint8_t>(CH_Flag + 48)};
            Relay_Analysis(Data, MQTT_Mode);
            commandHandled = true;
          } else if (Value == 0 && Relay_Flag[CH_Flag - 1]) {
            uint8_t Data[1] = {'0'};
            Relay_Analysis(Data, MQTT_Mode);
            commandHandled = true;
          }
        } else {
          if((Value == 1 && !Relay_Flag[CH_Flag - 1]) || (Value == 0 && Relay_Flag[CH_Flag - 1])){
            uint8_t Data[1] = {static_cast<uint8_t>(CH_Flag + 48)};
            Relay_Analysis(Data,MQTT_Mode);
            commandHandled = true;
          }
        }
      }
      else if(CH_Flag == 7){
        const bool allRelayOn = Relay_Flag[0] && Relay_Flag[1] && Relay_Flag[2] && Relay_Flag[3] && Relay_Flag[4] && Relay_Flag[5];
        const bool anyRelayOn = Relay_Flag[0] || Relay_Flag[1] || Relay_Flag[2] || Relay_Flag[3] || Relay_Flag[4] || Relay_Flag[5];
        if(Value == 1 && !allRelayOn){
          uint8_t Data[1] = {static_cast<uint8_t>(7 + 48)};
          Relay_Analysis(Data,MQTT_Mode);
          commandHandled = true;
        }
        else if(Value == 0 && anyRelayOn){
          uint8_t Data[1] = {static_cast<uint8_t>(8 + 48)};
          Relay_Analysis(Data,MQTT_Mode);
          commandHandled = true;
        }
      }
    }
  }

  if (commandHandled) {
    MQTT_MarkStateDirty();
    MQTT_PublishState(true);
  } else if (dataBegin == -1 && lowerInput.indexOf("\"cmd\"") == -1) {
    printf("Note : Non-instruction data was received - MQTT!\r\n");
  }
}

static const char* WIFI_CFG_FILE = "/wifi.cfg";

static bool EnsureWiFiConfigFS()
{
  static bool fsReady = false;
  static bool fsTried = false;
  if (!fsTried) {
    fsTried = true;
    fsReady = LittleFS.begin(false);
    if (!fsReady) {
      printf("warning: LittleFS mount failed, wifi config file disabled.\r\n");
    }
  }
  return fsReady;
}

static bool LoadWiFiConfigFromFile(String& outSsid, String& outPass)
{
  outSsid = "";
  outPass = "";
  if (!EnsureWiFiConfigFS()) {
    return false;
  }
  if (!LittleFS.exists(WIFI_CFG_FILE)) {
    return false;
  }

  File f = LittleFS.open(WIFI_CFG_FILE, "r");
  if (!f) {
    return false;
  }

  outSsid = f.readStringUntil('\n');
  outPass = f.readStringUntil('\n');
  f.close();
  outSsid.trim();
  outPass.trim();
  return outSsid.length() > 0;
}

static bool SaveWiFiConfigToFile(const String& ssidIn, const String& passIn)
{
  if (!EnsureWiFiConfigFS()) {
    return false;
  }
  String ss = ssidIn;
  String pp = passIn;
  ss.trim();
  pp.trim();
  if (ss.length() == 0) {
    return false;
  }

  File f = LittleFS.open(WIFI_CFG_FILE, "w");
  if (!f) {
    return false;
  }
  f.println(ss);
  f.println(pp);
  f.close();
  return true;
}

static bool waitForWiFiConnected(uint32_t timeoutMs) {
  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    printf(".\r\n");
    if ((millis() - startMs) >= timeoutMs) {
      return false;
    }
  }
  return true;
}


void setup_wifi() {
  WiFiManager wm;
  bool connected = false;
  bool connectedByPortal = false;
  String localSsid;
  String localPass;

  WiFi.persistent(true);
  WiFi.setAutoReconnect(true);
  WiFi.mode(WIFI_STA);

  if (LoadWiFiConfigFromFile(localSsid, localPass)) {
    printf("Connecting with local wifi config file: %s\r\n", localSsid.c_str());
    WiFi.begin(localSsid.c_str(), localPass.c_str());
    connected = waitForWiFiConnected(12000);
    if (!connected) {
      printf("Local wifi config failed.\r\n");
    }
  }

  if (!connected) {
    printf("Connecting with saved WiFi credentials...\r\n");
    WiFi.begin();
    connected = waitForWiFiConnected(8000);
  }

  if (!connected) {
    printf("Saved WiFi failed, trying preset SSID: %s\r\n", ssid);
    WiFi.disconnect(false, false);
    delay(100);
    WiFi.begin(ssid, password);
    connected = waitForWiFiConnected(12000);
  }

  if (!connected && WIFI_FallbackPortal_Enable) {
    printf("Preset WiFi failed, starting config portal...\r\n");
    wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);
    if (wm.autoConnect(WIFI_PORTAL_SSID, WIFI_PORTAL_PASSWORD)) {
      connected = true;
      connectedByPortal = true;
      printf("WiFi portal connected and credentials saved.\r\n");
    } else {
      printf("WiFi config portal timeout/fail.\r\n");
    }
  }

  if (!connected) {
    WIFI_Connection = 0;
    printf("WIFI connection fails, network features are unavailable in current mode.\r\n");
    RGB_Light(60, 0, 0);
    return;
  }

  WIFI_Connection = 1;

  const String curSsid = WiFi.SSID();
  const String curPass = WiFi.psk();
  if (SaveWiFiConfigToFile(curSsid, curPass)) {
    if (connectedByPortal) {
      printf("WiFi config file saved after portal connect.\r\n");
    }
  } else {
    printf("warning: failed to save wifi config file.\r\n");
  }

  RGB_Light(0, 60, 0);
  delay(1000);
  RGB_Light(0, 0, 0);

  IPAddress myIP = WiFi.localIP();
  printf("AP IP address: ");
  sprintf(ipStr, "%d.%d.%d.%d", myIP[0], myIP[1], myIP[2], myIP[3]);
  printf("%s\r\n", ipStr);

  // Register web routes
  server.on("/", handleRoot);
  server.on("/favicon.ico", [](){ server.send(204, "text/plain", ""); });
  server.on("/getData", handleGetData);
  server.on("/api/state", handleApiState);
  server.on("/api/cmd", HTTP_POST, handleApiCmd);
  server.on("/config", handleConfigPage);
  server.on("/api/config", HTTP_GET, handleApiConfigGet);
  server.on("/api/config", HTTP_POST, handleApiConfigPost);
  server.on("/Switch1", handleSwitch1);
  server.on("/Switch2", handleSwitch2);
  server.on("/Switch3", handleSwitch3);
  server.on("/Switch4", handleSwitch4);
  server.on("/Switch5", handleSwitch5);
  server.on("/Switch6", handleSwitch6);
  server.on("/AllOn", handleSwitch7);
  server.on("/AllOff", handleSwitch8);
  server.on("/GateOpen", handleGateOpen);
  server.on("/GateClose", handleGateClose);
  server.on("/GateStop", handleGateStop);
  server.on("/AutoGateOn", handleAutoGateOn);
  server.on("/AutoGateOff", handleAutoGateOff);
  server.on("/AutoGateLatchOff", handleAutoGateLatchOff);
  server.on("/ManualEnd", handleManualEnd);
  server.onNotFound([](){ server.send(404, "text/plain", "404 Not Found"); });
  if (ELEGANT_OTA_Enable) {
    ElegantOTA.begin(&server);
  }
  server.begin();

  printf("Web server started\r\n");
  printf("Web panel: http://%s/\r\n", ipStr);
  Ota_SetStatus("ElegantOTA", "ready_update_page");
  if (ELEGANT_OTA_Enable) {
    printf("ElegantOTA page: http://%s/update\r\n", ipStr);
  }
}
// Reconnect to the MQTT server (non-blocking, throttled)
void reconnect() {
  static uint32_t lastAttemptMs = 0;
  static uint8_t failCount = 0;
  const uint32_t retryIntervalMs = 5000;

  if (client.connected()) {
    return;
  }
  if (millis() - lastAttemptMs < retryIntervalMs) {
    return;
  }
  lastAttemptMs = millis();

  const bool connected = MQTT_HasAuth() ? client.connect(ID, mqtt_user, mqtt_password) : client.connect(ID);
  if (connected) {
    client.subscribe(sub);
    failCount = 0;
    MQTT_MarkStateDirty();
    MQTT_PublishState(true);
    printf("MQTT connected: server=%s port=%d sub=%s pub=%s\r\n", mqtt_server, PORT, sub, pub);
    return;
  }

  failCount++;
  if ((failCount % 6) == 0) {
    printf("warning: MQTT not connected, state=%d server=%s:%d\r\n", client.state(), mqtt_server, PORT);
  }
}
void MQTT_Init()
{
  setup_wifi();
  if(WIFI_Connection == 1 && MQTT_CLOUD_Enable){
    client.setServer(mqtt_server, PORT);
    client.setCallback(callback);
    // PubSubClient default buffer is too small for the /getData-style JSON payload.
    client.setBufferSize(3072);
  } else if (WIFI_Connection == 1) {
    printf("MQTT disabled, Web panel + ElegantOTA are available.\r\n");
  }
}

void MQTT_Loop()
{
  if(WIFI_Connection == 1){
    // Web
    server.handleClient();                                  // Processing requests from clients
    if (ELEGANT_OTA_Enable) {
      ElegantOTA.loop();
    }
    // MQTT
    if (MQTT_CLOUD_Enable) {
      if (!client.connected()) {
        reconnect();
      }
      client.loop();
      MQTT_PublishState(false);
    }
  }
}
































