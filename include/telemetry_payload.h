#pragma once

#include <Arduino.h>

#include "wifi_manager.h"

struct CellTowerInfo {
  int mcc;
  int mnc;
  int lac;
  int cid;
};

String buildTelemetryJson(const char *deviceId, float weightKg, float stableKg,
                          float batteryV, int batteryPct, int gsmRssi,
                          const CellTowerInfo &cell, const WifiLinkInfo &wifi,
                          unsigned long txIntervalSec);
