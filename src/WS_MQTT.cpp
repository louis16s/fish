#include "WS_MQTT.h"
#include <WiFiManager.h>
#include <LittleFS.h>
#include <ElegantOTA.h>
#include <cstring>
#include <ArduinoJson.h>

#include "WS_Control.h"
#include "WS_Log.h"
#include "WS_FS.h"
#include "WS_UI_Assets.h"

#ifndef CONTENT_LENGTH_UNKNOWN
#define CONTENT_LENGTH_UNKNOWN ((size_t)-1)
#endif

#ifndef MQTT_LOG_PUSH_Enable
#define MQTT_LOG_PUSH_Enable true
#endif

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
static bool g_httpRoutesRegistered = false;
static bool g_httpStarted = false;
static uint32_t g_wifiRetryLastMs = 0;

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
extern uint32_t Gate_Last_Action_EndMs;
extern char Gate_Block_Reason[96];
extern bool Manual_Takeover_Active;
extern uint32_t Manual_Takeover_UntilMs;
extern uint32_t Manual_Takeover_DurationMs;
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
extern void WS_Ctrl_ForceReload();
static char OtaLatestVersion[32] = "ElegantOTA";
static char OtaLastCheck[24] = "n/a";
static char OtaLastResult[96] = "web_update_only";
static bool Mqtt_State_Dirty = true;
static uint32_t Mqtt_LastPublishMs = 0;

static void WS_JsonEscape(const char* in, char* out, size_t outSize)
{
  if (out == nullptr || outSize == 0) {
    return;
  }
  out[0] = '\0';
  if (in == nullptr) {
    return;
  }

  size_t w = 0;
  for (size_t i = 0; in[i] != '\0' && (w + 1) < outSize; i++) {
    const char c = in[i];
    if (c == '\\' || c == '"') {
      if ((w + 2) >= outSize) {
        break;
      }
      out[w++] = '\\';
      out[w++] = c;
      continue;
    }
    // Strip control characters that could break JSON formatting.
    if ((unsigned char)c < 0x20) {
      out[w++] = ' ';
      continue;
    }
    out[w++] = c;
  }
  out[w] = '\0';
}

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
  const bool wifiStaConnected = (WiFi.status() == WL_CONNECTED);
  const int rssi = wifiStaConnected ? WiFi.RSSI() : -127;
  const bool mqttConnected = (MQTT_CLOUD_Enable && wifiStaConnected) ? client.connected() : false;
  const uint32_t nowMs = millis();

  char ssidEsc[80];
  WS_JsonEscape(wifiStaConnected ? WiFi.SSID().c_str() : "", ssidEsc, sizeof(ssidEsc));

  char ipLocal[16];
  {
    const IPAddress ip = wifiStaConnected ? WiFi.localIP() : WiFi.softAPIP();
    snprintf(ipLocal, sizeof(ipLocal), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  }

  char ipEsc[24];
  WS_JsonEscape(ipLocal, ipEsc, sizeof(ipEsc));

  char reasonEsc[192];
  WS_JsonEscape(Gate_Block_Reason, reasonEsc, sizeof(reasonEsc));

  char alarmEsc[256];
  WS_JsonEscape(Alarm_Text, alarmEsc, sizeof(alarmEsc));

  char fwCurEsc[64];
  WS_JsonEscape(FW_VERSION, fwCurEsc, sizeof(fwCurEsc));
  char fwLatestEsc[64];
  WS_JsonEscape(OtaLatestVersion, fwLatestEsc, sizeof(fwLatestEsc));
  char fwLastCheckEsc[64];
  WS_JsonEscape(OtaLastCheck, fwLastCheckEsc, sizeof(fwLastCheckEsc));
  char fwLastResultEsc[160];
  WS_JsonEscape(OtaLastResult, fwLastResultEsc, sizeof(fwLastResultEsc));

  uint32_t manualRemainS = 0;
  if (Manual_Takeover_Active && Manual_Takeover_UntilMs > nowMs) {
    manualRemainS = (Manual_Takeover_UntilMs - nowMs + 999UL) / 1000UL;
  }
  uint32_t manualTotalS = 0;
  if (Manual_Takeover_DurationMs > 0) {
    manualTotalS = (Manual_Takeover_DurationMs + 999UL) / 1000UL;
  }
  uint32_t cooldownRemainS = 0;
  if (!Manual_Takeover_Active) {
    const uint32_t cooldownMs = (uint32_t)GATE_MIN_ACTION_INTERVAL_S * 1000UL;
    const uint32_t untilMs = Gate_Last_Action_EndMs + cooldownMs;
    if (cooldownMs > 0 && (int32_t)(nowMs - untilMs) < 0) {
      cooldownRemainS = (untilMs - nowMs + 999UL) / 1000UL;
    }
  }
  uint32_t airLastRxAgeS = 0;
  if (Air780E_LastRxMs > 0 && nowMs >= Air780E_LastRxMs) {
    airLastRxAgeS = (nowMs - Air780E_LastRxMs) / 1000UL;
  }

  const int n = snprintf(
    json,
    jsonSize,
    "{\"sensor1\":{\"mm\":%u,\"valid\":%s,\"online\":%s,\"temp_x10\":%d,\"temp_valid\":%s},\"sensor2\":{\"mm\":%u,\"valid\":%s,\"online\":%s,\"temp_x10\":%d,\"temp_valid\":%s},\"gate_state\":%u,\"gate_position_open\":%s,\"auto_gate\":%s,\"auto_latched\":%s,\"manual\":{\"active\":%s,\"remain_s\":%lu,\"total_s\":%lu},\"relay1\":%u,\"relay2\":%u,\"net\":{\"wifi\":%s,\"mqtt\":%s,\"http\":%s,\"ip\":\"%s\",\"rssi\":%d,\"ssid\":\"%s\"},\"cell\":{\"enabled\":%s,\"online\":%s,\"sim_ready\":%s,\"attached\":%s,\"csq\":%d,\"rssi_dbm\":%d,\"last_rx_age_s\":%lu},\"ctrl\":{\"open_allowed\":%s,\"close_allowed\":%s,\"cooldown_remain_s\":%lu,\"min_interval_s\":%u,\"action_s\":%u,\"reason\":\"%s\"},\"alarm\":{\"active\":%s,\"severity\":%u,\"text\":\"%s\"},\"fw\":{\"current\":\"%s\",\"latest\":\"%s\",\"last_check\":\"%s\",\"last_result\":\"%s\"}}",
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
    (unsigned long)manualTotalS,
    Relay_Flag[0] ? 1 : 0,
    Relay_Flag[1] ? 1 : 0,
    wifiStaConnected ? "true" : "false",
    mqttConnected ? "true" : "false",
    g_httpStarted ? "true" : "false",
    ipEsc,
    rssi,
    ssidEsc,
    AIR780E_Enable ? "true" : "false",
    Air780E_Online ? "true" : "false",
    Air780E_SIMReady ? "true" : "false",
    Air780E_Attached ? "true" : "false",
    Air780E_CSQ,
    Air780E_RSSI_dBm,
    (unsigned long)airLastRxAgeS,
    Gate_Open_Allowed ? "true" : "false",
    Gate_Close_Allowed ? "true" : "false",
    (unsigned long)cooldownRemainS,
    (unsigned int)GATE_MIN_ACTION_INTERVAL_S,
    (unsigned int)GATE_RELAY_ACTION_SECONDS,
    reasonEsc,
    Alarm_Active ? "true" : "false",
    Alarm_Severity,
    alarmEsc,
    fwCurEsc,
    fwLatestEsc,
    fwLastCheckEsc,
    fwLastResultEsc
  );

  if (n < 0 || (size_t)n >= jsonSize) {
    // Keep response JSON valid even if it doesn't fit into the buffer.
    snprintf(json, jsonSize, "{\"ok\":false,\"error\":\"telemetry_json_overflow\"}");
  }
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

  char json[2000];
  MQTT_BuildStateJson(json, sizeof(json));
  if (client.publish(pub, json, false)) {
    Mqtt_LastPublishMs = nowMs;
    Mqtt_State_Dirty = false;
  }
}

// ===================== MQTT RPC Replies (Cloud Panel) =====================
// Cloud panel uses server-side MQTT and exposes HTTP APIs to browsers.
// Device replies are published to "<device_id>/device/reply".
static char g_mqttReplyTopic[96] = {0};

static const char* MQTT_ReplyTopic()
{
  if (g_mqttReplyTopic[0] != '\0') {
    return g_mqttReplyTopic;
  }
  // Derive topic from telemetry publish topic "<device_id>/device/telemetry".
  const char* p = pub;
  if (p && p[0] != '\0') {
    const char* slash = strchr(p, '/');
    const size_t didLen = slash ? (size_t)(slash - p) : strlen(p);
    if (didLen > 0 && didLen < 48) {
      snprintf(g_mqttReplyTopic, sizeof(g_mqttReplyTopic), "%.*s/device/reply", (int)didLen, p);
      return g_mqttReplyTopic;
    }
  }
  // Fallback to MQTT username (often equals device_id).
  if (mqtt_user && mqtt_user[0] != '\0') {
    snprintf(g_mqttReplyTopic, sizeof(g_mqttReplyTopic), "%s/device/reply", mqtt_user);
    return g_mqttReplyTopic;
  }
  snprintf(g_mqttReplyTopic, sizeof(g_mqttReplyTopic), "device/reply");
  return g_mqttReplyTopic;
}

static void MQTT_PublishReplyJson(const JsonDocument& doc)
{
  if (!MQTT_CLOUD_Enable || !client.connected()) {
    return;
  }
  const char* t = MQTT_ReplyTopic();
  if (!t || t[0] == '\0') {
    return;
  }
  String out;
  serializeJson(doc, out);
  (void)client.publish(t, out.c_str(), false);
}

// ===================== MQTT Log Push (Device -> Cloud) =====================
// Pushed log lines are published to "<device_id>/device/log/<name>" as plain text.
// This lets the cloud server cache logs locally and serve /api/log without an RPC round-trip.
static char g_mqttLogTopicBase[96] = {0};

static const char* MQTT_LogTopicBase()
{
  if (g_mqttLogTopicBase[0] != '\0') {
    return g_mqttLogTopicBase;
  }
  // Derive from telemetry publish topic "<device_id>/device/telemetry".
  const char* p = pub;
  if (p && p[0] != '\0') {
    const char* slash = strchr(p, '/');
    const size_t didLen = slash ? (size_t)(slash - p) : strlen(p);
    if (didLen > 0 && didLen < 48) {
      snprintf(g_mqttLogTopicBase, sizeof(g_mqttLogTopicBase), "%.*s/device/log", (int)didLen, p);
      return g_mqttLogTopicBase;
    }
  }
  // Fallback to MQTT username (often equals device_id).
  if (mqtt_user && mqtt_user[0] != '\0') {
    snprintf(g_mqttLogTopicBase, sizeof(g_mqttLogTopicBase), "%s/device/log", mqtt_user);
    return g_mqttLogTopicBase;
  }
  snprintf(g_mqttLogTopicBase, sizeof(g_mqttLogTopicBase), "device/log");
  return g_mqttLogTopicBase;
}

static void MQTT_PublishLogLine(const char* name, const char* line)
{
  if (!MQTT_CLOUD_Enable || !MQTT_LOG_PUSH_Enable || !client.connected()) {
    return;
  }
  if (!name || name[0] == '\0' || !line || line[0] == '\0') {
    return;
  }
  const char* base = MQTT_LogTopicBase();
  if (!base || base[0] == '\0') {
    return;
  }
  char topic[128];
  snprintf(topic, sizeof(topic), "%s/%s", base, name);
  (void)client.publish(topic, line, false);
}

static void WS_LogSink_Mqtt(const char* name, const char* line)
{
  // Avoid any logging inside this sink to prevent recursion.
  MQTT_PublishLogLine(name, line);
}

static void MQTT_RpcReplyError(const char* reqId, const char* cmd, const char* error)
{
  if (!reqId || reqId[0] == '\0') {
    return; // no correlation id => no reply expected
  }
  JsonDocument doc;
  doc["ok"] = false;
  doc["req_id"] = reqId;
  if (cmd && cmd[0] != '\0') {
    doc["cmd"] = cmd;
  }
  doc["error"] = (error && error[0] != '\0') ? error : "error";
  MQTT_PublishReplyJson(doc);
}

static void MQTT_RpcReplyOk(const char* reqId, const char* cmd)
{
  if (!reqId || reqId[0] == '\0') {
    return;
  }
  JsonDocument doc;
  doc["ok"] = true;
  doc["req_id"] = reqId;
  if (cmd && cmd[0] != '\0') {
    doc["cmd"] = cmd;
  }
  MQTT_PublishReplyJson(doc);
}

// ===================== UI Files (LittleFS Preferred, Embedded Fallback) =====================
// UI files are served from LittleFS under /ui/... when present, otherwise from
// firmware-embedded assets generated by scripts/embed_ui_assets.py.
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

static bool WS_HTTP_StreamFileFromEmbedded(const char* path)
{
  const WS_UI_Asset* a = WS_UI_FindAsset(path);
  if (a == nullptr || a->data == nullptr || a->len == 0) {
    return false;
  }
  server.sendHeader("Cache-Control", "no-store");
  // `send_P` streams from flash/PROGMEM and supports binary data when length is provided.
  server.send_P(200, (PGM_P)a->content_type, (PGM_P)a->data, a->len);
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
  html += "<p>未找到对应 UI 文件（LittleFS 与固件内置资源均未命中）。</p>";
  html += "<p>可选修复：</p>";
  html += "<ol>";
  html += "<li>重新烧录固件（确保已运行 scripts/embed_ui_assets.py 生成内置 UI 资源）；</li>";
  html += "<li>或在 PlatformIO 执行：<b>Upload Filesystem Image</b>（LittleFS），把 <code>data/</code>（包含 <code>data/ui/</code>）上传到设备。</li>";
  html += "</ol>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

static void WS_HTTP_SendUiPage(const char* path)
{
  if (!Http_Auth()) return;
  if (WS_HTTP_StreamFileFromLittleFS(path, "text/html; charset=utf-8")) return;
  if (WS_HTTP_StreamFileFromEmbedded(path)) return;
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

void handleApiLogDownload()
{
  if (!Http_Auth()) {
    return;
  }
  String name = server.hasArg("name") ? server.arg("name") : "";
  name.toLowerCase();
  const char* basePath = LogPathFromName(name);
  if (!basePath) {
    server.send(400, "text/plain", "bad name");
    return;
  }

  const bool bak = server.hasArg("bak") ? (server.arg("bak").toInt() != 0) : false;
  String path = String(basePath) + (bak ? ".1" : "");

  if (!WS_FS_EnsureMounted()) {
    server.send(500, "text/plain", "fs not mounted");
    return;
  }
  if (!LittleFS.exists(path.c_str())) {
    server.send(404, "text/plain", "not found");
    return;
  }
  File f = LittleFS.open(path.c_str(), "r");
  if (!f) {
    server.send(500, "text/plain", "open failed");
    return;
  }

  const String filename = String("log_") + name + (bak ? "_1" : "") + ".txt";
  const String cd = String("attachment; filename=\"") + filename + "\"";
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Disposition", cd);
  server.streamFile(f, "text/plain; charset=utf-8");
  f.close();
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
  char json[2000];
  MQTT_BuildStateJson(json, sizeof(json));
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleApiState()
{
  if (!Http_Auth()) {
    return;
  }
  char tele[2000];
  MQTT_BuildStateJson(tele, sizeof(tele));

  const bool wifiStaConnected = (WiFi.status() == WL_CONNECTED);
  const bool mqttConnected = (MQTT_CLOUD_Enable && wifiStaConnected) ? client.connected() : false;

  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{\"mqtt_connected\":");
  server.sendContent(mqttConnected ? "true" : "false");
  server.sendContent(",\"last_telemetry_at\":");
  char msBuf[16];
  snprintf(msBuf, sizeof(msBuf), "%lu", (unsigned long)millis());
  server.sendContent(msBuf);
  server.sendContent(",\"telemetry\":");
  server.sendContent(tele);
  server.sendContent("}");
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
  WS_Ctrl_ForceReload();
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

static void WS_Net_UpdateIpStrFromWiFi()
{
  // Prefer STA IP, otherwise use AP IP (if any).
  const bool sta = (WiFi.status() == WL_CONNECTED);
  const IPAddress ip = sta ? WiFi.localIP() : WiFi.softAPIP();
  snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

static void WS_HTTP_RegisterRoutesOnce()
{
  if (g_httpRoutesRegistered) {
    return;
  }
  g_httpRoutesRegistered = true;

  server.on("/", handleRoot);
  server.on("/favicon.ico", [](){ server.send(204, "text/plain", ""); });
  server.on("/getData", handleGetData);
  server.on("/api/state", handleApiState);
  server.on("/api/cmd", HTTP_POST, handleApiCmd);
  server.on("/logs", handleLogsPage);
  server.on("/api/log", HTTP_GET, handleApiLogGet);
  server.on("/api/log/clear", HTTP_POST, handleApiLogClear);
  server.on("/api/log/download", HTTP_GET, handleApiLogDownload);
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
      if (WS_HTTP_StreamFileFromEmbedded(uri.c_str())) {
        return;
      }
    }
    server.send(404, "text/plain", "404 Not Found");
  });
  if (ELEGANT_OTA_Enable) {
    ElegantOTA.begin(&server);
  }
}

static void WS_HTTP_BeginOnce()
{
  if (g_httpStarted) {
    return;
  }
  WS_HTTP_RegisterRoutesOnce();
  server.begin();
  g_httpStarted = true;
}
/************************************************** MQTT *********************************************/
// MQTT subscribes to callback functions for processing received messages
void callback(char* topic, byte* payload, unsigned int length) {
  (void)topic;
  String inputString;
  inputString.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) {
    inputString += (char)payload[i];
  }
  printf("%s\r\n", inputString.c_str()); // Supported formats: {"data":{"CH1":1}} / {"cmd":"gate_open"}

  bool anyHandled = false;
  bool stateChanged = false;

  // Prefer strict JSON parsing (required for cloud config/log RPC).
  {
    JsonDocument doc;
    const DeserializationError err = deserializeJson(doc, inputString);
    if (!err) {
      const char* reqId = doc["req_id"] | "";
      const char* cmd = doc["cmd"];

      if (cmd && cmd[0] != '\0') {
        String c = String(cmd);
        c.trim();
        c.toLowerCase();

        if (HandleCmdString(c)) {
          anyHandled = true;
          stateChanged = true;
          MQTT_RpcReplyOk(reqId, c.c_str());
        } else if (c == "get_config") {
          anyHandled = true;
          String raw = WS_Control_LoadRawJson();
          if (raw.length() == 0) {
            WS_ControlConfig cfg;
            (void)WS_Control_Load(cfg);
            raw = WS_Control_LoadRawJson();
          }
          if (reqId && reqId[0] != '\0') {
            JsonDocument rep;
            rep["ok"] = true;
            rep["req_id"] = reqId;
            rep["cmd"] = "get_config";
            rep["raw"] = raw;
            MQTT_PublishReplyJson(rep);
          }
        } else if (c == "set_config") {
          anyHandled = true;
          String rawIn;
          if (!doc["raw"].isNull()) {
            rawIn = String((const char*)(doc["raw"] | ""));
          } else if (doc["config"].is<JsonObject>()) {
            serializeJsonPretty(doc["config"], rawIn);
          } else if (doc["cfg"].is<JsonObject>()) {
            serializeJsonPretty(doc["cfg"], rawIn);
          }
          rawIn.trim();
          if (rawIn.length() == 0) {
            MQTT_RpcReplyError(reqId, "set_config", "missing_raw");
          } else if (!WS_Control_SaveRawJson(rawIn.c_str())) {
            MQTT_RpcReplyError(reqId, "set_config", "invalid_json");
          } else {
            WS_Ctrl_ForceReload();
            stateChanged = true;
            MQTT_RpcReplyOk(reqId, "set_config");
          }
        } else if (c == "get_log") {
          anyHandled = true;
          String name = String((const char*)(doc["name"] | "error"));
          name.toLowerCase();
          const bool bak = ((int)(doc["bak"] | 0)) != 0;
          long tailL = (long)(doc["tail"] | 16384);
          if (tailL < 0) tailL = 0;
          if (tailL > 32768) tailL = 32768;

          const char* basePath = LogPathFromName(name);
          if (!basePath) {
            MQTT_RpcReplyError(reqId, "get_log", "bad_name");
          } else {
            const String path = String(basePath) + (bak ? ".1" : "");
            String out;
            if (!ReadFileTailToString(path.c_str(), (size_t)tailL, out)) {
              MQTT_RpcReplyError(reqId, "get_log", "read_failed");
            } else if (reqId && reqId[0] != '\0') {
              JsonDocument rep;
              rep["ok"] = true;
              rep["req_id"] = reqId;
              rep["cmd"] = "get_log";
              rep["name"] = name;
              rep["bak"] = bak ? 1 : 0;
              rep["text"] = out;
              MQTT_PublishReplyJson(rep);
            }
          }
        } else if (c == "clear_log") {
          anyHandled = true;
          String name = String((const char*)(doc["name"] | "error"));
          name.toLowerCase();
          const char* path = LogPathFromName(name);
          if (!path) {
            MQTT_RpcReplyError(reqId, "clear_log", "bad_name");
          } else if (!TruncateFile(path)) {
            MQTT_RpcReplyError(reqId, "clear_log", "clear_failed");
          } else {
            MQTT_RpcReplyOk(reqId, "clear_log");
          }
        } else {
          anyHandled = true;
          MQTT_RpcReplyError(reqId, cmd, "unknown_cmd");
        }
      }

      // Relay control format: {"data":{"CH1":1}}.
      if (doc["data"].is<JsonObject>()) {
        JsonObject data = doc["data"].as<JsonObject>();
        int chFlag = 0;
        int val = 0;
        if (!data["CH1"].isNull()) { chFlag = 1; val = data["CH1"] | 0; }
        else if (!data["CH2"].isNull()) { chFlag = 2; val = data["CH2"] | 0; }
        else if (!data["CH3"].isNull()) { chFlag = 3; val = data["CH3"] | 0; }
        else if (!data["CH4"].isNull()) { chFlag = 4; val = data["CH4"] | 0; }
        else if (!data["CH5"].isNull()) { chFlag = 5; val = data["CH5"] | 0; }
        else if (!data["CH6"].isNull()) { chFlag = 6; val = data["CH6"] | 0; }
        else if (!data["ALL"].isNull()) { chFlag = 7; val = data["ALL"] | 0; }

        bool did = false;
        if (chFlag != 0) {
          if (chFlag < 7) {
            if (chFlag == 1 || chFlag == 2) {
              if (val == 1) {
                uint8_t Data[1] = { static_cast<uint8_t>(chFlag + 48) };
                Relay_Analysis(Data, MQTT_Mode);
                did = true;
              } else if (val == 0 && Relay_Flag[chFlag - 1]) {
                uint8_t Data[1] = { '0' };
                Relay_Analysis(Data, MQTT_Mode);
                did = true;
              }
            } else {
              if ((val == 1 && !Relay_Flag[chFlag - 1]) || (val == 0 && Relay_Flag[chFlag - 1])) {
                uint8_t Data[1] = { static_cast<uint8_t>(chFlag + 48) };
                Relay_Analysis(Data, MQTT_Mode);
                did = true;
              }
            }
          } else if (chFlag == 7) {
            const bool allRelayOn = Relay_Flag[0] && Relay_Flag[1] && Relay_Flag[2] && Relay_Flag[3] && Relay_Flag[4] && Relay_Flag[5];
            const bool anyRelayOn = Relay_Flag[0] || Relay_Flag[1] || Relay_Flag[2] || Relay_Flag[3] || Relay_Flag[4] || Relay_Flag[5];
            if (val == 1 && !allRelayOn) {
              uint8_t Data[1] = { static_cast<uint8_t>(7 + 48) };
              Relay_Analysis(Data, MQTT_Mode);
              did = true;
            } else if (val == 0 && anyRelayOn) {
              uint8_t Data[1] = { static_cast<uint8_t>(8 + 48) };
              Relay_Analysis(Data, MQTT_Mode);
              did = true;
            }
          }
        }

        if (did) {
          anyHandled = true;
          stateChanged = true;
        }
      }

      if (stateChanged) {
        MQTT_MarkStateDirty();
        MQTT_PublishState(true);
      }
      if (!anyHandled) {
        printf("Note : Non-instruction data was received - MQTT!\\r\\n");
      }
      return;
    }
  }

  // Fallback legacy parser (keeps backward compatibility if JSON parse fails).
  uint8_t CH_Flag = 0;
  bool commandHandled = false;
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

  const int dataBegin = inputString.indexOf("\"data\"");
  if (dataBegin != -1) {
    int CH_Begin = -1;
    if (inputString.indexOf("\"CH1\"", dataBegin) != -1){ CH_Flag = 1; CH_Begin = inputString.indexOf("\"CH1\"", dataBegin); }
    else if (inputString.indexOf("\"CH2\"", dataBegin) != -1){ CH_Flag = 2; CH_Begin = inputString.indexOf("\"CH2\"", dataBegin); }
    else if (inputString.indexOf("\"CH3\"", dataBegin) != -1){ CH_Flag = 3; CH_Begin = inputString.indexOf("\"CH3\"", dataBegin); }
    else if (inputString.indexOf("\"CH4\"", dataBegin) != -1){ CH_Flag = 4; CH_Begin = inputString.indexOf("\"CH4\"", dataBegin); }
    else if (inputString.indexOf("\"CH5\"", dataBegin) != -1){ CH_Flag = 5; CH_Begin = inputString.indexOf("\"CH5\"", dataBegin); }
    else if (inputString.indexOf("\"CH6\"", dataBegin) != -1){ CH_Flag = 6; CH_Begin = inputString.indexOf("\"CH6\"", dataBegin); }
    else if (inputString.indexOf("\"ALL\"", dataBegin) != -1){ CH_Flag = 7; CH_Begin = inputString.indexOf("\"ALL\"", dataBegin); }

    int valueBegin = -1;
    if (CH_Begin != -1) {
      valueBegin = inputString.indexOf(':', CH_Begin);
    }
    int valueEnd = -1;
    if (valueBegin != -1) {
      const int valueEndComma = inputString.indexOf(',', valueBegin + 1);
      const int valueEndBrace = inputString.indexOf('}', valueBegin + 1);
      if (valueEndComma == -1) valueEnd = valueEndBrace;
      else if (valueEndBrace == -1) valueEnd = valueEndComma;
      else valueEnd = (valueEndComma < valueEndBrace) ? valueEndComma : valueEndBrace;
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
    printf("Note : Non-instruction data was received - MQTT!\\r\\n");
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
    printf("WIFI connection fails, starting fallback AP for local web panel...\r\n");
    RGB_Light(60, 0, 0);

    if (WIFI_FallbackPortal_Enable) {
      // Keep AP available so the user can still access the panel even when STA is down.
      WiFi.mode(WIFI_AP_STA);
      const bool apOk = WiFi.softAP(WIFI_PORTAL_SSID, WIFI_PORTAL_PASSWORD);
      if (!apOk) {
        printf("warning: fallback AP start failed.\r\n");
        return;
      }

      WS_Net_UpdateIpStrFromWiFi();
      WS_HTTP_BeginOnce();

      printf("Web server started (AP)\r\n");
      printf("Web panel (AP): http://%s/\r\n", ipStr[0] ? ipStr : "192.168.4.1");
      Ota_SetStatus("ElegantOTA", "ap_mode");
      if (ELEGANT_OTA_Enable) {
        printf("ElegantOTA page (AP): http://%s/update\r\n", ipStr[0] ? ipStr : "192.168.4.1");
      }
    } else {
      printf("Network fallback is disabled; web panel is unavailable.\r\n");
    }
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

  // Register web routes and start web server (once).
  WS_HTTP_BeginOnce();

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
  if (MQTT_CLOUD_Enable) {
    client.setServer(mqtt_server, PORT);
    client.setCallback(callback);
    // PubSubClient default buffer is too small for the /getData-style JSON payload.
    client.setBufferSize(3072);
    WS_Log_SetLineSink(WS_LogSink_Mqtt);
  } else {
    WS_Log_SetLineSink(nullptr);
    printf("MQTT disabled, Web panel + ElegantOTA are available.\r\n");
  }
}

void MQTT_Loop()
{
  // Web: keep responsive even when STA is offline (may still be reachable via SoftAP).
  if (g_httpStarted) {
    server.handleClient();
    if (ELEGANT_OTA_Enable) {
      ElegantOTA.loop();
    }
  }

  // MQTT: only meaningful when STA is connected.
  if (!MQTT_CLOUD_Enable) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    const uint32_t now = millis();
    if ((now - g_wifiRetryLastMs) > 30000UL) {
      g_wifiRetryLastMs = now;
      (void)WiFi.reconnect();
      WS_Net_UpdateIpStrFromWiFi();
    }
    return;
  }

  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  MQTT_PublishState(false);
}































