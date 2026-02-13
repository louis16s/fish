#include "WS_Serial.h"
#include <cstring>
#include <cstdlib>

HardwareSerial lidarSerial(1);     // UART1 for RS485 ultrasonic sensors
HardwareSerial air780eSerial(2);   // UART2 for Air780E (AT)

bool Air780E_Online = false;
bool Air780E_SIMReady = false;
bool Air780E_Attached = false;
int Air780E_CSQ = -1;
int Air780E_RSSI_dBm = -127;
uint32_t Air780E_LastRxMs = 0;

static uint32_t Air780E_LastProbeMs = 0;
static uint32_t Air780E_LastLogMs = 0;
static uint8_t Air780E_ProbeStep = 0;
static char Air780E_LastLine[128] = "";
static char Air780E_LineBuf[160] = {0};
static uint8_t Air780E_LineLen = 0;

static int Air780E_CsqToDbm(int csq)
{
  if (csq < 0 || csq > 31) {
    return -127;
  }
  return -113 + csq * 2;
}

static void Air780E_SendCommand(const char* cmd)
{
  if (!AIR780E_Enable || cmd == nullptr || cmd[0] == '\0') {
    return;
  }
  air780eSerial.print(cmd);
  air780eSerial.print("\r\n");
}

static void Air780E_ParseLine(char* line)
{
  if (line == nullptr) {
    return;
  }

  while (*line == ' ' || *line == '\t') {
    line++;
  }

  size_t len = strlen(line);
  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n' || line[len - 1] == ' ')) {
    line[len - 1] = '\0';
    len--;
  }

  if (line[0] == '\0') {
    return;
  }

  snprintf(Air780E_LastLine, sizeof(Air780E_LastLine), "%s", line);
  Air780E_LastRxMs = millis();
  Air780E_Online = true;

  if (strstr(line, "+CPIN:") != nullptr) {
    Air780E_SIMReady = (strstr(line, "READY") != nullptr);
    return;
  }

  if (strstr(line, "+CGATT:") != nullptr) {
    const char* p = strchr(line, ':');
    if (p != nullptr) {
      const int val = atoi(p + 1);
      Air780E_Attached = (val == 1);
    }
    return;
  }

  if (strstr(line, "+CSQ:") != nullptr) {
    const char* p = strchr(line, ':');
    if (p != nullptr) {
      int csq = atoi(p + 1);
      if (csq < 0 || csq > 31) {
        csq = -1;
      }
      Air780E_CSQ = csq;
      Air780E_RSSI_dBm = Air780E_CsqToDbm(csq);
    }
    return;
  }
}

static void Air780E_ReadLoop()
{
  while (air780eSerial.available() > 0) {
    const char ch = static_cast<char>(air780eSerial.read());

    if (ch == '\r') {
      continue;
    }

    if (ch == '\n') {
      if (Air780E_LineLen > 0) {
        Air780E_LineBuf[Air780E_LineLen] = '\0';
        Air780E_ParseLine(Air780E_LineBuf);
        Air780E_LineLen = 0;
      }
      continue;
    }

    if (Air780E_LineLen < (sizeof(Air780E_LineBuf) - 1U)) {
      Air780E_LineBuf[Air780E_LineLen++] = ch;
    }
  }

  if (Air780E_LastRxMs == 0) {
    return;
  }

  if ((millis() - Air780E_LastRxMs) > AIR780E_ONLINE_TIMEOUT_MS) {
    Air780E_Online = false;
    Air780E_Attached = false;
    Air780E_CSQ = -1;
    Air780E_RSSI_dBm = -127;
  }
}

static void Air780E_ProbeLoop()
{
  const uint32_t now = millis();
  if ((now - Air780E_LastProbeMs) < AIR780E_POLL_INTERVAL_MS) {
    return;
  }
  Air780E_LastProbeMs = now;

  switch (Air780E_ProbeStep) {
    case 0:
      Air780E_SendCommand("AT");
      break;
    case 1:
      Air780E_SendCommand("AT+CPIN?");
      break;
    case 2:
      Air780E_SendCommand("AT+CSQ");
      break;
    default:
      Air780E_SendCommand("AT+CGATT?");
      break;
  }

  Air780E_ProbeStep = (Air780E_ProbeStep + 1U) % 4U;
}

static void Air780E_LogLoop()
{
  const uint32_t now = millis();
  if ((now - Air780E_LastLogMs) < AIR780E_LOG_INTERVAL_MS) {
    return;
  }
  Air780E_LastLogMs = now;

  uint32_t lastRxAgeS = 0;
  if (Air780E_LastRxMs > 0) {
    lastRxAgeS = (now - Air780E_LastRxMs) / 1000UL;
  }

  printf("[Air780E] online=%d sim=%d attached=%d csq=%d rssi=%ddBm last_rx=%lus last='%s'\r\n",
         Air780E_Online ? 1 : 0,
         Air780E_SIMReady ? 1 : 0,
         Air780E_Attached ? 1 : 0,
         Air780E_CSQ,
         Air780E_RSSI_dBm,
         static_cast<unsigned long>(lastRxAgeS),
         Air780E_LastLine);
}

void Serial_Init()
{
  lidarSerial.begin(9600, SERIAL_8N1, RXD1, TXD1);

  if (!AIR780E_Enable) {
    return;
  }

  air780eSerial.begin(AIR780E_BAUDRATE, SERIAL_8N1, AIR780E_RXD, AIR780E_TXD);
  Air780E_Online = false;
  Air780E_SIMReady = false;
  Air780E_Attached = false;
  Air780E_CSQ = -1;
  Air780E_RSSI_dBm = -127;
  Air780E_LastRxMs = 0;
  Air780E_LastProbeMs = millis() - AIR780E_POLL_INTERVAL_MS;
  Air780E_LastLogMs = millis();
  Air780E_ProbeStep = 0;
  Air780E_LineLen = 0;
  snprintf(Air780E_LastLine, sizeof(Air780E_LastLine), "boot");

  Air780E_SendCommand("ATE0");
  Air780E_SendCommand("AT");
  printf("Air780E UART ready. RX=IO%d TX=IO%d Baud=%d\r\n", AIR780E_RXD, AIR780E_TXD, AIR780E_BAUDRATE);
}

void Air780E_Loop()
{
  if (!AIR780E_Enable) {
    return;
  }

  Air780E_ReadLoop();
  Air780E_ProbeLoop();
  Air780E_LogLoop();
}



