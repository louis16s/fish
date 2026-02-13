#include "WS_MQTT.h"
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ElegantOTA.h>
#include <cstring>
#include <ArduinoJson.h>

#include "WS_Control.h"
#include "WS_Log.h"
#include "WS_FS.h"

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

// ===================== UI Files On LittleFS =====================
// Front-end pages are served from LittleFS to keep C++ code small and allow UI updates via uploadfs.
static const char* kUiIndexPath = "/ui/index.html";
static const char* kUiConfigPath = "/ui/config.html";
static const char* kUiLogsPath = "/ui/logs.html";

static bool WS_HTTP_StreamFileFromLittleFS(const char* path, const String& contentType)
{
  if (!WS_FS_EnsureMounted()) return false;
  if (!LittleFS.exists(path)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, contentType);
  f.close();
  return true;
}

static void WS_HTTP_SendUiMissingHint(const char* wanted)
{
  String html;
  html.reserve(700);
  html += "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>UI 缺失</title></head>";
  html += "<body style=\"font-family:system-ui,-apple-system,Segoe UI,Roboto,PingFang SC,Microsoft YaHei,sans-serif;padding:18px\">";
  html += "<h2>UI 文件缺失</h2>";
  html += "<p>未找到：<code>";
  html += wanted;
  html += "</code></p>";
  html += "<p>请在 PlatformIO 执行：<b>Upload Filesystem Image</b>（LittleFS），把 <code>data/</code>（包含 <code>data/ui/</code>）上传到设备。</p>";
  html += "<p>上传后刷新本页即可。</p>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

static void WS_HTTP_SendUiPage(const char* path)
{
  if (!Http_Auth()) return;
  if (WS_HTTP_StreamFileFromLittleFS(path, "text/html; charset=utf-8")) return;
  WS_HTTP_SendUiMissingHint(path);
}

static String WS_HTTP_GuessContentType(const String& path)
{
  if (path.endsWith(".html")) return "text/html; charset=utf-8";
  if (path.endsWith(".css")) return "text/css; charset=utf-8";
  if (path.endsWith(".js")) return "application/javascript; charset=utf-8";
  if (path.endsWith(".json")) return "application/json; charset=utf-8";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".ico")) return "image/x-icon";
  if (path.endsWith(".svg")) return "image/svg+xml";
  return "application/octet-stream";
}

void handleRoot() {
  WS_HTTP_SendUiPage(kUiIndexPath);
  return;
#if 0
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
    .top-actions{display:flex;gap:10px;align-items:center}
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

    .bars{margin-top:12px;display:grid;grid-template-columns:1fr 1fr;gap:12px}
    .barbox{position:relative;height:220px;border:1px solid var(--line);border-radius:12px;background:linear-gradient(180deg,#ffffff 0%, #f8fafc 100%);overflow:hidden}
    .barfill{position:absolute;left:0;right:0;bottom:0;height:0%;background:linear-gradient(180deg,#22c55e 0%, #0ea5e9 100%);transition:height .45s ease}
    .barfill.offline{background:linear-gradient(180deg,#fecaca 0%, #fb7185 100%)}
    .scale{position:absolute;inset:0;pointer-events:none}
    .scale i{position:absolute;left:0;right:0;border-top:1px dashed rgba(15,23,42,.12)}
    .scale b{position:absolute;right:8px;transform:translateY(-50%);font-size:11px;color:var(--muted);font-weight:900}
    .barlabel{display:flex;align-items:center;justify-content:space-between;margin-top:8px;font-size:12px;color:var(--muted);font-weight:900}
    @media (max-width:900px){.grid{grid-template-columns:1fr}}
    @media (max-width:520px){.controls{grid-template-columns:1fr 1fr}}
    @media (max-width:520px){.bars{grid-template-columns:1fr}}
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
    <div class="top-actions">
      <a class="btn" href="/logs" style="text-decoration:none;display:inline-grid;place-items:center">日志</a>
      <a class="btn" href="/config" style="text-decoration:none;display:inline-grid;place-items:center">配置</a>
    </div>
  </header>

  <main class="grid">
    <section class="card">
      <div class="card-title">水位（绝对比例 0-5m）</div>
      <div class="kv">
        <div class="k">内塘</div><div class="v" id="innerLevel">--</div>
        <div class="k">外塘</div><div class="v" id="outerLevel">--</div>
        <div class="k">水位差</div><div class="v" id="delta">--</div>
      </div>

      <div class="bars">
        <div>
          <div class="barbox">
            <div class="barfill" id="barInner"></div>
            <div class="scale">
              <i style="top:0%"></i><b style="top:0%">5m</b>
              <i style="top:20%"></i><b style="top:20%">4</b>
              <i style="top:40%"></i><b style="top:40%">3</b>
              <i style="top:60%"></i><b style="top:60%">2</b>
              <i style="top:80%"></i><b style="top:80%">1</b>
              <i style="top:100%"></i><b style="top:100%">0</b>
            </div>
          </div>
          <div class="barlabel"><span>内塘</span><span id="innerM">--</span></div>
        </div>
        <div>
          <div class="barbox">
            <div class="barfill" id="barOuter"></div>
            <div class="scale">
              <i style="top:0%"></i><b style="top:0%">5m</b>
              <i style="top:20%"></i><b style="top:20%">4</b>
              <i style="top:40%"></i><b style="top:40%">3</b>
              <i style="top:60%"></i><b style="top:60%">2</b>
              <i style="top:80%"></i><b style="top:80%">1</b>
              <i style="top:100%"></i><b style="top:100%">0</b>
            </div>
          </div>
          <div class="barlabel"><span>外塘</span><span id="outerM">--</span></div>
        </div>
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
      <div class="hint" id="metaLine">固件版本 __FW_VERSION__</div>
    </section>
  </main>

  <script>
    const $ = (id) => document.getElementById(id);
    function setStatus(t){ $('statusLine').textContent=t; }
    function gateStateText(s){ if(s===1) return '开闸中'; if(s===2) return '关闸中'; return '待机'; }
    function clamp(n, a, b){ return Math.min(b, Math.max(a, n)); }
    function setBar(id, mm, valid){
      const el = $(id);
      if(!el) return;
      const maxMm = 5000;
      const v = valid ? clamp(mm, 0, maxMm) : 0;
      const pct = (v / maxMm) * 100;
      el.style.height = pct.toFixed(1) + '%';
      el.className = valid ? 'barfill' : 'barfill offline';
    }
    function render(t){
      if(!t){ return; }
      const s1=t.sensor1||{}, s2=t.sensor2||{};
      const innerOk=!!s1.valid, outerOk=!!s2.valid;
      const innerMm=(s1.mm||0), outerMm=(s2.mm||0);
      const innerM = innerOk ? (innerMm/1000.0) : null;
      const outerM = outerOk ? (outerMm/1000.0) : null;
      $('innerLevel').textContent = innerOk ? (innerMm+' mm ('+innerM.toFixed(3)+' m)') : '离线';
      $('outerLevel').textContent = outerOk ? (outerMm+' mm ('+outerM.toFixed(3)+' m)') : '离线';
      $('innerM').textContent = innerOk ? (innerM.toFixed(3)+' m') : '--';
      $('outerM').textContent = outerOk ? (outerM.toFixed(3)+' m') : '--';
      setBar('barInner', innerMm, innerOk);
      setBar('barOuter', outerMm, outerOk);
      $('delta').textContent = (innerOk && outerOk) ? ((innerMm-outerMm)+' mm') : '--';
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
#endif
}

static const char* kLogErrorPath = "/log_error.txt";
static const char* kLogMeasurePath = "/log_measure.txt";
static const char* kLogActionPath = "/log_action.txt";

static const char* LogPathFromName(const String& name)
{
  if (name == "error") return kLogErrorPath;
  if (name == "measure") return kLogMeasurePath;
  if (name == "action") return kLogActionPath;
  return nullptr;
}

static bool ReadFileTailToString(const char* path, size_t tailBytes, String& out)
{
  out = "";
  if (!WS_FS_EnsureMounted()) return false;
  if (!LittleFS.exists(path)) return true; // empty
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  const size_t sz = (size_t)f.size();
  size_t start = 0;
  if (tailBytes > 0 && sz > tailBytes) start = sz - tailBytes;
  if (start > 0) (void)f.seek(start, SeekSet);
  out.reserve((tailBytes > 0) ? tailBytes : sz);
  while (f.available()) {
    out += (char)f.read();
  }
  f.close();
  return true;
}

static bool TruncateFile(const char* path)
{
  if (!WS_FS_EnsureMounted()) return false;
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  f.close();
  return true;
}

void handleLogsPage()
{
  WS_HTTP_SendUiPage(kUiLogsPath);
  return;
#if 0
  if (!Http_Auth()) {
    return;
  }
  const char* page = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>日志</title>
  <style>
    :root{--bg:#f6f7fb;--card:#fff;--line:#e5e7eb;--text:#0f172a;--muted:#475569;--accent:#0ea5e9;--warn:#d97706;--bad:#dc2626}
    *{box-sizing:border-box}
    body{margin:0;font-family:ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,"PingFang SC","Microsoft YaHei",sans-serif;background:var(--bg);color:var(--text);padding:18px}
    .wrap{max-width:980px;margin:0 auto}
    .top{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:12px}
    .btn{appearance:none;border:1px solid var(--line);background:#fff;color:var(--text);border-radius:12px;padding:10px 12px;font-weight:900;cursor:pointer;min-height:44px;text-decoration:none;display:inline-grid;place-items:center}
    .btn.danger{border-color:#dc2626;background:#fef2f2;color:#7f1d1d}
    .card{border:1px solid var(--line);border-radius:14px;background:var(--card);padding:14px;box-shadow:0 12px 30px rgba(15,23,42,.08)}
    .tabs{display:flex;gap:8px;flex-wrap:wrap;margin-bottom:12px}
    .tab{appearance:none;border:1px solid var(--line);background:#fff;color:var(--muted);border-radius:999px;padding:8px 10px;font-weight:900;cursor:pointer}
    .tab.on{border-color:#0284c7;background:rgba(14,165,233,.10);color:var(--text)}
    pre{margin:0;border:1px solid var(--line);border-radius:12px;background:#0b1020;color:#e2e8f0;padding:12px;min-height:520px;max-height:70vh;overflow:auto;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:12px;line-height:1.5}
    .hint{margin-top:10px;color:var(--muted);font-size:12px;line-height:1.6}
  </style>
</head>
<body>
  <div class="wrap">
    <div class="top">
      <a class="btn" href="/">返回面板</a>
      <div style="display:flex;gap:10px">
        <button class="btn" onclick="refresh()">刷新</button>
        <button class="btn danger" onclick="clearCur()">清空当前日志</button>
      </div>
    </div>
    <div class="card">
      <div class="tabs">
        <button class="tab on" id="t_error" onclick="setTab('error')">错误日志</button>
        <button class="tab" id="t_measure" onclick="setTab('measure')">测量日志</button>
        <button class="tab" id="t_action" onclick="setTab('action')">动作日志</button>
      </div>
      <pre id="logBox">加载中…</pre>
      <div class="hint">说明：日志存储在设备 LittleFS。默认只读取末尾 16KB，可用于快速排查。</div>
      <div class="hint" id="msg"></div>
    </div>
  </div>
  <script>
    let cur='error';
    const $=(id)=>document.getElementById(id);
    function setTab(t){
      cur=t;
      ['error','measure','action'].forEach(k=>{
        $('t_'+k).className = (k===t) ? 'tab on' : 'tab';
      });
      refresh();
    }
    async function refresh(){
      try{
        const r = await fetch('/api/log?name='+encodeURIComponent(cur)+'&tail=16384');
        const tx = await r.text();
        $('logBox').textContent = tx.length ? tx : '(空)';
        $('msg').textContent = r.ok ? '' : ('读取失败: '+tx);
        $('logBox').scrollTop = $('logBox').scrollHeight;
      }catch(e){
        $('msg').textContent = '读取失败';
      }
    }
    async function clearCur(){
      if(!confirm('确认清空当前日志？')) return;
      try{
        const r = await fetch('/api/log/clear', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({name:cur})});
        $('msg').textContent = r.ok ? '已清空' : ('清空失败: '+(await r.text()));
      }catch(e){
        $('msg').textContent = '清空失败';
      }
      refresh();
    }
    setInterval(refresh, 2000);
    refresh();
  </script>
</body>
</html>
)HTML";
  server.send(200, "text/html", page);
#endif
}

void handleApiLogGet()
{
  if (!Http_Auth()) {
    return;
  }
  String name = server.hasArg("name") ? server.arg("name") : "";
  name.toLowerCase();
  const char* path = LogPathFromName(name);
  if (!path) {
    server.send(400, "text/plain", "bad name");
    return;
  }
  long tailL = server.hasArg("tail") ? server.arg("tail").toInt() : 16384;
  if (tailL < 0) tailL = 0;
  if (tailL > 65536) tailL = 65536;
  const size_t tail = (size_t)tailL;
  String out;
  if (!ReadFileTailToString(path, tail, out)) {
    server.send(500, "text/plain", "read failed");
    return;
  }
  server.send(200, "text/plain; charset=utf-8", out);
}

static bool ParseNameFromJsonBody(String& outName)
{
  outName = "";
  if (!server.hasArg("plain")) return false;
  JsonDocument doc;
  const String body = server.arg("plain");
  DeserializationError err = deserializeJson(doc, body);
  if (err) return false;
  outName = String((const char*)(doc["name"] | ""));
  outName.toLowerCase();
  return outName.length() > 0;
}

void handleApiLogClear()
{
  if (!Http_Auth()) {
    return;
  }
  String name;
  if (!ParseNameFromJsonBody(name)) {
    server.send(400, "text/plain", "invalid json");
    return;
  }
  const char* path = LogPathFromName(name);
  if (!path) {
    server.send(400, "text/plain", "bad name");
    return;
  }
  if (!TruncateFile(path)) {
    server.send(500, "text/plain", "clear failed");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
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
  WS_HTTP_SendUiPage(kUiConfigPath);
  return;
#if 0
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
    :root{--bg:#f6f7fb;--card:#fff;--line:#e5e7eb;--text:#0f172a;--muted:#475569;--accent:#0ea5e9;--bad:#dc2626}
    *{box-sizing:border-box}
    body{margin:0;font-family:ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,"PingFang SC","Microsoft YaHei",sans-serif;background:var(--bg);color:var(--text);padding:18px}
    .wrap{max-width:980px;margin:0 auto}
    .top{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:12px}
    .btn{appearance:none;border:1px solid var(--line);background:#fff;color:var(--text);border-radius:12px;padding:10px 12px;font-weight:900;cursor:pointer;min-height:44px;text-decoration:none;display:inline-grid;place-items:center}
    .btn.primary{border-color:#0284c7;background:var(--accent);color:#fff}
    .btn.danger{border-color:#dc2626;background:#fef2f2;color:#7f1d1d}
    .card{border:1px solid var(--line);border-radius:14px;background:var(--card);padding:14px;box-shadow:0 12px 30px rgba(15,23,42,.08)}
    .grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}
    .sec{border:1px solid var(--line);border-radius:12px;padding:12px;background:#f8fafc}
    .sec-title{font-weight:900;margin-bottom:10px}
    .row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
    label{font-size:12px;color:var(--muted);font-weight:900}
    input,select{border:1px solid var(--line);border-radius:12px;padding:10px 12px;font-weight:900;min-height:44px;background:#fff;color:var(--text)}
    input[type="number"]{width:120px}
    input[type="time"]{width:140px}
    select{min-width:160px}
    .list{display:grid;gap:10px}
    .item{border:1px solid var(--line);border-radius:12px;padding:12px;background:#fff}
    .item-top{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:10px}
    .item-title{font-weight:900}
    .mini{font-size:12px;color:var(--muted);font-weight:900}
    .btn.small{min-height:36px;padding:8px 10px;border-radius:999px}
    textarea{width:100%;min-height:260px;border:1px solid var(--line);border-radius:12px;padding:10px 12px;font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:13px}
    .hint{margin-top:10px;color:var(--muted);font-size:12px;line-height:1.6}
    details{margin-top:12px}
    @media (max-width:900px){.grid{grid-template-columns:1fr}}
  </style>
</head>
<body>
  <div class="wrap">
    <div class="top">
      <a class="btn" href="/">返回面板</a>
      <div class="row">
        <button class="btn" onclick="loadCfg()">重新加载</button>
        <button class="btn primary" onclick="saveCfg()">保存</button>
      </div>
    </div>

    <div class="card">
      <div class="sec-title">控制配置</div>

      <div class="grid">
        <div class="sec">
          <div class="sec-title">基本</div>
          <div class="row">
            <label>模式</label>
            <select id="mode">
              <option value="mixed">混合(推荐)</option>
              <option value="daily">仅定时</option>
              <option value="cycle">仅循环</option>
              <option value="leveldiff">仅水位差</option>
            </select>
            <label>时区(小时)</label>
            <input id="tz_h" type="number" min="-12" max="14" step="1" value="8">
          </div>
          <div class="hint">混合模式：如果任意循环规则启用，则优先循环；否则触发定时；最后用水位差作为持续兜底。</div>
        </div>

        <div class="sec">
          <div class="sec-title">说明</div>
          <div class="hint">
            1. 定时：多组，开与关可分离启用。
            <br>2. 循环：多段间隔，支持多组(最多2组，按顺序取第一组启用的)。
            <br>3. 水位差：delta = 内塘(mm) - 外塘(mm)。
          </div>
        </div>
      </div>

      <div style="height:12px"></div>

      <div class="sec">
        <div class="item-top">
          <div>
            <div class="item-title">定时规则</div>
            <div class="mini">例如：08:00 开，09:00 关。</div>
          </div>
          <button class="btn small" onclick="addDaily()">添加定时</button>
        </div>
        <div class="list" id="dailyList"></div>
      </div>

      <div style="height:12px"></div>

      <div class="sec">
        <div class="item-top">
          <div>
            <div class="item-title">循环规则</div>
            <div class="mini">例如：开 8小时，关 3小时，再开 5小时。</div>
          </div>
          <button class="btn small" onclick="addCycleRule()">添加一组循环</button>
        </div>
        <div class="list" id="cycleList"></div>
      </div>

      <div style="height:12px"></div>

      <div class="sec">
        <div class="item-top">
          <div>
            <div class="item-title">水位差规则</div>
            <div class="mini">当 delta <= 打开阈值 时开闸；当 delta >= 关闭阈值 时关闸。</div>
          </div>
          <button class="btn small" onclick="addLevelDiff()">添加水位差</button>
        </div>
        <div class="list" id="ldList"></div>
      </div>

      <details>
        <summary class="mini">高级：查看/编辑原始 JSON</summary>
        <textarea id="cfgRaw"></textarea>
      </details>

      <div class="hint">配置文件存储在设备 LittleFS：`/ctrl.json`。保存后立即生效。</div>
      <div class="hint" id="msg"></div>
    </div>
  </div>

  <script>
    const $=(id)=>document.getElementById(id);
    const num=(v,def)=>{ const n=parseInt(v,10); return Number.isFinite(n)?n:def; };
    const timeToStr=(t)=> (t && t.length===5) ? t : '08:00';

    let model = {tz_offset_s:28800, mode:'mixed', daily:[], cycle:[], leveldiff:[]};

    function dailyTpl(i){
      return `
        <div class="item">
          <div class="item-top">
            <div class="item-title">定时 #${i+1}</div>
            <button class="btn small danger" onclick="delDaily(${i})">删除</button>
          </div>
          <div class="row">
            <label><input type="checkbox" id="d_en_${i}"> 启用</label>
            <label><input type="checkbox" id="d_open_en_${i}"> 开闸</label>
            <input type="time" id="d_open_${i}">
            <label><input type="checkbox" id="d_close_en_${i}"> 关闸</label>
            <input type="time" id="d_close_${i}">
          </div>
        </div>`;
    }

    function cycleRuleTpl(ri){
      return `
        <div class="item">
          <div class="item-top">
            <div class="item-title">循环组 #${ri+1}</div>
            <div class="row">
              <button class="btn small" onclick="addCycleStep(${ri})">添加步骤</button>
              <button class="btn small danger" onclick="delCycleRule(${ri})">删除本组</button>
            </div>
          </div>
          <div class="row" style="margin-bottom:10px">
            <label><input type="checkbox" id="c_en_${ri}"> 启用本组</label>
            <span class="mini">提示：只取第一组启用的循环。</span>
          </div>
          <div class="list" id="cycleSteps_${ri}"></div>
        </div>`;
    }

    function cycleStepTpl(ri, si){
      return `
        <div class="item" style="background:#f8fafc">
          <div class="item-top">
            <div class="mini">步骤 #${si+1}</div>
            <button class="btn small danger" onclick="delCycleStep(${ri},${si})">删除步骤</button>
          </div>
          <div class="row">
            <label>动作</label>
            <select id="c_${ri}_state_${si}">
              <option value="open">开闸</option>
              <option value="close">关闸</option>
            </select>
            <label>时长</label>
            <input id="c_${ri}_h_${si}" type="number" min="0" max="999" step="1" value="0"><span class="mini">小时</span>
            <input id="c_${ri}_m_${si}" type="number" min="0" max="59" step="1" value="0"><span class="mini">分钟</span>
          </div>
        </div>`;
    }

    function ldTpl(i){
      return `
        <div class="item">
          <div class="item-top">
            <div class="item-title">水位差 #${i+1}</div>
            <button class="btn small danger" onclick="delLevelDiff(${i})">删除</button>
          </div>
          <div class="row">
            <label><input type="checkbox" id="l_en_${i}"> 启用</label>
            <label>打开阈值(mm)</label>
            <input id="l_open_${i}" type="number" step="1" value="-1">
            <label>关闭阈值(mm)</label>
            <input id="l_close_${i}" type="number" step="1" value="0">
            <span class="mini">默认：-1/0 表示 内塘低于外塘开闸，内塘高于或等于外塘关闸。</span>
          </div>
        </div>`;
    }

    function render(){
      $('mode').value = model.mode || 'mixed';
      $('tz_h').value = Math.round((model.tz_offset_s||28800)/3600);

      const daily = (model.daily||[]).slice(0,8);
      $('dailyList').innerHTML = daily.map((_,i)=>dailyTpl(i)).join('');
      daily.forEach((r,i)=>{
        $('d_en_'+i).checked = !!r.en;
        $('d_open_en_'+i).checked = (r.open_en!==false);
        $('d_close_en_'+i).checked = (r.close_en!==false);
        $('d_open_'+i).value = timeToStr(r.open||'08:00');
        $('d_close_'+i).value = timeToStr(r.close||'09:00');
      });

      const cycle = (model.cycle||[]).slice(0,2);
      $('cycleList').innerHTML = cycle.map((_,ri)=>cycleRuleTpl(ri)).join('');
      cycle.forEach((r,ri)=>{
        $('c_en_'+ri).checked = !!r.en;
        const steps = (r.steps||[]).slice(0,10);
        $('cycleSteps_'+ri).innerHTML = steps.map((_,si)=>cycleStepTpl(ri,si)).join('');
        steps.forEach((st,si)=>{
          const min = num(st.min, 60);
          const h = Math.floor(min/60), m = min%60;
          $('c_'+ri+'_state_'+si).value = (st.state==='close') ? 'close' : 'open';
          $('c_'+ri+'_h_'+si).value = h;
          $('c_'+ri+'_m_'+si).value = m;
        });
      });

      const ld = (model.leveldiff||[]).slice(0,4);
      $('ldList').innerHTML = ld.map((_,i)=>ldTpl(i)).join('');
      ld.forEach((r,i)=>{
        $('l_en_'+i).checked = !!r.en;
        $('l_open_'+i).value = num(r.open_mm, -1);
        $('l_close_'+i).value = num(r.close_mm, 0);
      });

      $('cfgRaw').value = JSON.stringify(model, null, 2);
    }

    function collect(){
      const tzH = num($('tz_h').value, 8);
      const mode = $('mode').value || 'mixed';

      const daily=[];
      for(let i=0;i<$('dailyList').children.length;i++){
        daily.push({
          en: $('d_en_'+i).checked,
          open_en: $('d_open_en_'+i).checked,
          open: timeToStr($('d_open_'+i).value),
          close_en: $('d_close_en_'+i).checked,
          close: timeToStr($('d_close_'+i).value),
        });
      }

      const cycle=[];
      for(let ri=0;ri<$('cycleList').children.length;ri++){
        const steps=[];
        for(let si=0;si<$('cycleSteps_'+ri).children.length;si++){
          const state = $('c_'+ri+'_state_'+si).value === 'close' ? 'close' : 'open';
          const h = Math.max(0, num($('c_'+ri+'_h_'+si).value, 0));
          const m = Math.max(0, Math.min(59, num($('c_'+ri+'_m_'+si).value, 0)));
          const min = Math.max(1, h*60+m);
          steps.push({state, min});
        }
        cycle.push({en:$('c_en_'+ri).checked, steps});
      }

      const leveldiff=[];
      for(let i=0;i<$('ldList').children.length;i++){
        leveldiff.push({
          en: $('l_en_'+i).checked,
          open_mm: num($('l_open_'+i).value, -1),
          close_mm: num($('l_close_'+i).value, 0),
        });
      }

      return {tz_offset_s: tzH*3600, mode, daily, cycle, leveldiff};
    }

    // UI actions
    function addDaily(){
      model.daily = model.daily || [];
      if(model.daily.length>=8){ $('msg').textContent='定时最多 8 组'; return; }
      model.daily.push({en:true, open_en:true, open:'08:00', close_en:true, close:'09:00'});
      render();
    }
    function delDaily(i){ model.daily.splice(i,1); render(); }

    function addCycleRule(){
      model.cycle = model.cycle || [];
      if(model.cycle.length>=2){ $('msg').textContent='循环最多 2 组'; return; }
      model.cycle.push({en:false, steps:[{state:'open',min:480},{state:'close',min:180},{state:'open',min:300}]});
      render();
    }
    function delCycleRule(i){ model.cycle.splice(i,1); render(); }
    function addCycleStep(ri){
      const r = model.cycle[ri];
      r.steps = r.steps || [];
      if(r.steps.length>=10){ $('msg').textContent='每组最多 10 段'; return; }
      r.steps.push({state:'open',min:60});
      render();
    }
    function delCycleStep(ri,si){ model.cycle[ri].steps.splice(si,1); render(); }

    function addLevelDiff(){
      model.leveldiff = model.leveldiff || [];
      if(model.leveldiff.length>=4){ $('msg').textContent='水位差最多 4 组'; return; }
      model.leveldiff.push({en:true, open_mm:-1, close_mm:0});
      render();
    }
    function delLevelDiff(i){ model.leveldiff.splice(i,1); render(); }

    async function loadCfg(){
      $('msg').textContent='加载中…';
      try{
        const t = await (await fetch('/api/config')).text();
        model = JSON.parse(t);
        render();
        $('msg').textContent='';
      }catch(e){
        $('msg').textContent='加载失败：配置 JSON 解析失败';
      }
    }

    async function saveCfg(){
      try{
        let bodyObj;
        try{
          bodyObj = JSON.parse($('cfgRaw').value || '');
        }catch(e){
          bodyObj = collect();
        }
        const body = JSON.stringify(bodyObj, null, 2);
        const r = await fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'}, body});
        $('msg').textContent = r.ok ? '已保存' : ('保存失败: '+(await r.text()));
        if(r.ok){ model=bodyObj; render(); }
      }catch(e){
        $('msg').textContent='保存失败';
      }
    }

    // expose for inline onclick
    window.addDaily=addDaily; window.delDaily=delDaily;
    window.addCycleRule=addCycleRule; window.delCycleRule=delCycleRule;
    window.addCycleStep=addCycleStep; window.delCycleStep=delCycleStep;
    window.addLevelDiff=addLevelDiff; window.delLevelDiff=delLevelDiff;
    window.loadCfg=loadCfg; window.saveCfg=saveCfg;

    loadCfg();
  </script>
</body>
</html>
)HTML";
  server.send(200, "text/html", page);
#endif
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

static bool LoadWiFiConfigFromFile(String& outSsid, String& outPass)
{
  outSsid = "";
  outPass = "";
  if (!WS_FS_EnsureMounted()) {
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
  if (!WS_FS_EnsureMounted()) {
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
  server.on("/logs", handleLogsPage);
  server.on("/api/log", HTTP_GET, handleApiLogGet);
  server.on("/api/log/clear", HTTP_POST, handleApiLogClear);
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
  server.onNotFound([](){
    const String uri = server.uri();
    if (uri.startsWith("/ui/")) {
      if (!Http_Auth()) {
        return;
      }
      const String ct = WS_HTTP_GuessContentType(uri);
      if (WS_HTTP_StreamFileFromLittleFS(uri.c_str(), ct)) {
        return;
      }
    }
    server.send(404, "text/plain", "404 Not Found");
  });
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
































