#pragma once

#include <Arduino.h>

#include "wifi_manager.h"

struct CellTowerInfo {
  int mcc;
  int mnc;
  int lac;
  int cid;
};

// tempScaleC may be NAN (sensor missing/failed) — emitted as JSON null.
// boostKeepOn is the last verified IP5306 SYS_CTL0 keep-on read-back.
// reportTimeIso8601 is rtcClockNowIso8601() — empty if no clock has been
// synced yet, emitted as JSON null.
String buildTelemetryJson(const char *deviceId, float weightKg, float stableKg,
                          float tempScaleC, float batteryV, int batteryPct,
                          bool boostKeepOn, int gsmRssi, const CellTowerInfo &cell,
                          const WifiLinkInfo &wifi, unsigned long txIntervalSec,
                          const String &reportTimeIso8601);
