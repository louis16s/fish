#ifndef _WS_MQTT_H_
#define _WS_MQTT_H_

#include <PubSubClient.h>
#include <WiFi.h>
#include <WebServer.h>
#include "WS_GPIO.h"
#include "WS_Information.h"

#define MQTT_Mode            3
#define WIFI_Mode            3

extern PubSubClient client;
extern WiFiClient espClient;
extern WebServer server;

extern bool Relay_Flag[6];       // Relay current status flag
extern bool Gate_AutoControl_Enabled;
extern void Relay_Analysis(uint8_t *buf,uint8_t Mode_Flag);

/************************************************** Web *********************************************/
void handleRoot();
void handleGetData();
void handleApiState();
void handleApiCmd();
void handleConfigPage();
void handleApiConfigGet();
void handleApiConfigPost();
void handleSwitch(int ledNumber);

void handleSwitch1();
void handleSwitch2();
void handleSwitch3();
void handleSwitch4();
void handleSwitch5();
void handleSwitch6();
void handleSwitch7();
void handleSwitch8();
void handleGateOpen();
void handleGateClose();
void handleGateStop();
void handleAutoGateOn();
void handleAutoGateOff();
void handleAutoGateLatchOff();
void handleManualEnd();

/************************************************** MQTT *********************************************/
void callback(char* topic, byte* payload, unsigned int length);   // MQTT callback
void setup_wifi();
void reconnect();                                                 // Reconnect to MQTT server
void MQTT_Init();
void MQTT_Loop();

#endif




