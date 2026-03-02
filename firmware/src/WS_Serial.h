#ifndef _WS_SERIAL_H_
#define _WS_SERIAL_H_

#include <HardwareSerial.h>
#include <stdint.h>
#include "WS_GPIO.h"
#include "WS_Information.h"

extern HardwareSerial lidarSerial;
extern HardwareSerial air780eSerial;

extern bool Air780E_Online;
extern bool Air780E_SIMReady;
extern bool Air780E_Attached;
extern int Air780E_CSQ;
extern int Air780E_RSSI_dBm;
extern uint32_t Air780E_LastRxMs;

void Serial_Init();                           // Initialize RS485 and Air780E UART
void Air780E_Loop();                          // Non-blocking Air780E AT heartbeat and status parser

#endif
