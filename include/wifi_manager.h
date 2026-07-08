#pragma once

#include <stdint.h>

struct WifiLinkInfo {
  bool connected;
  char hostname[48];
  char ip[16];
  int rssi;
};

void wifiManagerBegin();
bool wifiManagerConnect(unsigned long timeoutMs = 15000);
void wifiManagerDisconnect();
void wifiManagerLoop();
bool wifiManagerIsConnected();
WifiLinkInfo wifiManagerStatus();
void wifiManagerShow();
