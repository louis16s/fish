#include "WS_Bluetooth.h"
#include "WS_MQTT.h"
#include <cstring>
#include <string>

#if BLUETOOTH_Enable
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#endif

extern bool WIFI_Connection;
extern char ipStr[16];

#if BLUETOOTH_Enable
namespace {
const char* BLE_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
const char* BLE_CHAR_RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // write
const char* BLE_CHAR_TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // notify

BLEServer* g_server = nullptr;
BLECharacteristic* g_txChar = nullptr;
bool g_connected = false;
bool g_prevConnected = false;
uint32_t g_lastIpReportMs = 0;

void HandleBluetoothCommand(const uint8_t* data, size_t len)
{
  if (data == nullptr || len == 0) {
    return;
  }

  for (size_t i = 0; i < len; ++i) {
    const uint8_t ch = data[i];
    if (ch < '0' || ch > '8') {
      continue;
    }
    uint8_t cmd[1] = {ch};
    Relay_Analysis(cmd, Bluetooth_Mode);
  }
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    g_connected = true;
    (void)pServer;
  }

  void onDisconnect(BLEServer* pServer) override {
    g_connected = false;
    (void)pServer;
  }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    std::string value = pCharacteristic->getValue();
    if (!value.empty()) {
      HandleBluetoothCommand(reinterpret_cast<const uint8_t*>(value.data()), value.size());
    }
  }
};

void NotifyText(const char* text)
{
  if (!g_connected || g_txChar == nullptr || text == nullptr) {
    return;
  }
  g_txChar->setValue((uint8_t*)text, strlen(text));
  g_txChar->notify();
}
}  // namespace
#endif

void Bluetooth_Init()
{
#if BLUETOOTH_Enable
  BLEDevice::init(BLUETOOTH_DEVICE_NAME);
  g_server = BLEDevice::createServer();
  g_server->setCallbacks(new ServerCallbacks());

  BLEService* service = g_server->createService(BLE_SERVICE_UUID);
  g_txChar = service->createCharacteristic(
    BLE_CHAR_TX_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  g_txChar->addDescriptor(new BLE2902());

  BLECharacteristic* rxChar = service->createCharacteristic(
    BLE_CHAR_RX_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  rxChar->setCallbacks(new RxCallbacks());

  service->start();
  g_server->getAdvertising()->start();

  g_connected = false;
  g_prevConnected = false;
  g_lastIpReportMs = 0;

  printf("Bluetooth BLE ready: %s\r\n", BLUETOOTH_DEVICE_NAME);
#else
  printf("Bluetooth is disabled in WS_Information.h\r\n");
#endif
}

void Bluetooth_Loop()
{
#if BLUETOOTH_Enable
  if (!g_connected && g_prevConnected && g_server != nullptr) {
    delay(150);
    g_server->startAdvertising();
    g_prevConnected = g_connected;
  }

  if (g_connected && !g_prevConnected) {
    g_prevConnected = g_connected;
    NotifyText("BLE connected");
  }

  if (BLUETOOTH_IP_REPORT_Enable && g_connected && WIFI_Connection) {
    const uint32_t now = millis();
    if ((now - g_lastIpReportMs) >= (uint32_t)BLUETOOTH_IP_REPORT_INTERVAL_MS) {
      g_lastIpReportMs = now;
      String ipMsg = String("WIFI IP: ") + ipStr;
      NotifyText(ipMsg.c_str());
    }
  }
#endif
}

void Bluetooth_SendData(const char* text)
{
#if BLUETOOTH_Enable
  NotifyText(text);
#else
  (void)text;
#endif
}

bool Bluetooth_IsConnected()
{
#if BLUETOOTH_Enable
  return g_connected;
#else
  return false;
#endif
}


