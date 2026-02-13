#ifndef _WS_LOG_H_
#define _WS_LOG_H_

#include <stdint.h>

// Logs are stored on LittleFS:
// - /log_error.txt
// - /log_measure.txt
// - /log_action.txt

void WS_Log_Init();

void WS_Log_Error(const char* fmt, ...);
void WS_Log_Measure(const char* fmt, ...);
void WS_Log_Action(const char* fmt, ...);

// Optional: set a human-readable timestamp prefix for log lines.
// If not set, logs will use millis().
void WS_Log_SetTimeProvider(uint32_t (*nowEpoch)());

#endif

