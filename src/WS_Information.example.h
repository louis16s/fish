#ifndef _WS_INFORMATION_H_
#define _WS_INFORMATION_H_

#include "auto_fw_version.h"

// This is an example configuration file.
// For real builds, copy/rename this file to WS_Information.h and fill in your values.
// IMPORTANT: Do not commit real passwords to GitHub.

// ===================== Feature Switches =====================
#define MQTT_CLOUD_Enable          true            // true: enable MQTT client
#define WIFI_FallbackPortal_Enable true            // true: start AP config portal when WiFi fails

// ===================== Preset WiFi =====================
#define STASSID       "YOUR_WIFI_SSID"
#define STAPSK        "YOUR_WIFI_PASSWORD"

// ===================== WiFi Config Portal (WiFiManager) =====================
#define WIFI_PORTAL_SSID           "Fish-esp32s3-Setup"
#define WIFI_PORTAL_PASSWORD       "CHANGE_ME"
#define WIFI_PORTAL_TIMEOUT_S      20

// ===================== Firmware =====================
#ifndef FW_VERSION
#define FW_VERSION                 "v0.0.0"
#endif
#define ELEGANT_OTA_Enable         true

// ===================== Network RGB =====================
#define WIFI_OFFLINE_RGB_BLINK_Enable      true
#define WIFI_OFFLINE_RGB_BLINK_INTERVAL_MS 500UL

// ===================== HTTP Auth (Device Web Panel) =====================
// Note: This protects the ESP32 local web panel (http://<device-ip>/).
#define HTTP_AUTH_Enable          false
#define HTTP_AUTH_Username        "admin"
#define HTTP_AUTH_Password        "CHANGE_ME"

// ===================== MQTT =====================
// Suggest: use your VPS public IP or domain.
#define MQTT_Server                  "67.209.185.215"
#define MQTT_Port                    1883
#define MQTT_ID                      "fish1-esp32s3"
#define MQTT_Username                "fish1"
#define MQTT_Password                "CHANGE_ME"
#define MQTT_Pub                     "fish1/device/telemetry"
#define MQTT_Sub                     "fish1/device/command"
#define MQTT_TELEMETRY_INTERVAL_MS   3000UL
#define MQTT_PUBLISH_ON_CHANGE_Enable true

// ===================== 4G Module (Air780E AT) =====================
#define AIR780E_Enable             false
#define AIR780E_BAUDRATE           115200
#define AIR780E_POLL_INTERVAL_MS   5000UL
#define AIR780E_ONLINE_TIMEOUT_MS  30000UL
#define AIR780E_LOG_INTERVAL_MS    15000UL

// ===================== Startup Buzzer =====================
#define STARTUP_BUZZER_Enable      true
#define STARTUP_BUZZER_DURATION_MS 1500

// ===================== Water Gate Control =====================
  // Sensor role mapping: ID001 = inner pond, ID002 = outer pond
  #define INNER_POND_SENSOR_ID          0x01
  #define OUTER_POND_SENSOR_ID          0x02
  #define GATE_AUTO_CONTROL_Enable      true
  #define GATE_RELAY_ACTION_SECONDS     10
  #define GATE_LEVEL_EQUAL_TOL_MM       30
  #define GATE_OPEN_DELTA_THRESHOLD_MM  (-60)   // inner-outer <= this -> open gate
  #define GATE_CLOSE_DELTA_THRESHOLD_MM (-20)   // inner-outer >= this -> close gate (hysteresis)
  #define GATE_MIN_ACTION_INTERVAL_S    15      // cooldown between actions
#define GATE_MAX_CONTINUOUS_RUN_S     260     // overtime stop protection
#define MANUAL_TAKEOVER_RECOVER_S     120     // after manual op, auto pauses and resumes later

// ===================== Sensor Safety =====================
#define SENSOR_DATA_TIMEOUT_MS         6000
#define SENSOR_ONLINE_GRACE_MS         3000   // only report sensor offline when no valid frame for > this duration
#define SENSOR_MODBUS_TIMEOUT_MS       250    // single Modbus RTU response timeout (ms)
#define SENSOR_MODBUS_RETRY_GAP_MS     80     // delay between Modbus retries (ms)
#define SENSOR_MODBUS_RETRY_COUNT      2      // 2 = first try + 1 retry
#define LEVEL_JUMP_THRESHOLD_MM_PER_S  1000
#define LEVEL_MIN_MM                   0
#define LEVEL_MAX_MM                   10000

// ===================== Serial Log =====================
#define SERIAL_LEVEL_LOG_Enable             true
#define SERIAL_LEVEL_LOG_INTERVAL_MS        8000
#define SERIAL_GATE_LOG_Enable              false
#define SERIAL_GATE_LOG_BLOCK_INTERVAL_MS   5000

#endif
