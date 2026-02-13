#ifndef _WS_BLUETOOTH_H_
#define _WS_BLUETOOTH_H_

#include <stdint.h>
#include "WS_Information.h"

#define Bluetooth_Mode 2

void Bluetooth_Init();
void Bluetooth_Loop();
void Bluetooth_SendData(const char* text);
bool Bluetooth_IsConnected();

#endif
