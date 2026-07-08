#pragma once

#include <TinyGsmClient.h>

#include "telemetry_payload.h"

struct ModemStatus {
  bool powered;
  bool initialized;
  bool simReady;
  bool networkRegistered;
  bool gprsConnected;
  int rssi;
  char operatorName[32];
  char imei[20];
  char modemInfo[64];
  char gprsIp[16];
  CellTowerInfo cell;
};

void modemManagerBeginHardware();
bool modemManagerPowerOn();
bool modemManagerPowerOff();
bool modemManagerRestart();
bool modemManagerWaitForNetwork(unsigned long timeoutMs);
bool modemManagerGprsConnect();
bool modemManagerGprsDisconnect();
bool modemManagerTcpTest(const char *host, uint16_t port, unsigned long timeoutMs);
bool modemManagerRunGprsTest(unsigned long networkTimeoutMs, unsigned long tcpTimeoutMs);
bool modemManagerEnsureGprs(unsigned long networkTimeoutMs);
bool modemManagerSyncClock();
bool modemManagerInstallCaCertificate(const uint8_t *data, size_t len);
TinyGsm &modemManagerModem();
TinyGsmClient &modemManagerGsmClient();
bool modemManagerUpdateStatus();
bool modemManagerRunTest(unsigned long networkTimeoutMs);
const ModemStatus &modemManagerStatus();
int modemManagerRssi();
bool modemManagerIsNetworkRegistered();
bool modemManagerIsGprsConnected();
void modemManagerPump();
#ifdef __cplusplus
extern "C" {
#endif
void beekprModemPumpForTls(void);
void beekprTlsDiagnostic(void);
#ifdef __cplusplus
}
#endif
void modemManagerShow();
