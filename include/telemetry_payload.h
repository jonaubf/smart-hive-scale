#pragma once

#include <Arduino.h>

#include "wifi_manager.h"

struct CellTowerInfo {
  int mcc;
  int mnc;
  int lac;
  int cid;
};

// cornersKg points at NUM_CORNERS per-corner weights (mux channels 0-3,
// emitted as corner1_kg..corner4_kg); NAN entries — a failed corner or an
// uncalibrated scale — are emitted as JSON null (FR-11).
// tempScaleC may be NAN (sensor missing/failed) — emitted as JSON null.
// reportTimeIso8601 is rtcClockNowIso8601() — empty if no clock has been
// synced yet, emitted as JSON null.
String buildTelemetryJson(const char *deviceId, float weightKg, float stableKg,
                          const float *cornersKg, float tempScaleC,
                          float batteryV, int batteryPct,
                          int gsmRssi, const CellTowerInfo &cell,
                          const WifiLinkInfo &wifi, unsigned long txIntervalSec,
                          const String &reportTimeIso8601);
