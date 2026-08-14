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
// SNTP → system clock, so rtcClockSyncFromSystemTimeIfNeeded() can write a
// trustworthy time into the DS3231 at sleep entry — WiFi mode's counterpart
// to modemManagerSyncClock(). Call after a successful STA connect.
bool wifiManagerSyncClock();
void wifiManagerDisconnect();
void wifiManagerLoop();
bool wifiManagerIsConnected();
WifiLinkInfo wifiManagerStatus();
void wifiManagerShow();
