#pragma once

#include <stdint.h>

#include "config.h"

// 4-corner calibration (spec.md §10): every corner keeps its own tare
// offset (mounting stress differs per corner even on a rigid frame); one
// shared span factor converts raw counts to kg, derived from a single known
// weight summed across all 4 tared corners. NVS-persisted.

// One full read of every corner plus conversion — the one-stop call for
// bench display, the portal, and the publish-cycle snapshot.
struct ScaleReading {
  bool ok;                      // every corner read successfully
  bool cornerOk[NUM_CORNERS];
  long cornerRaw[NUM_CORNERS];
  float cornerKg[NUM_CORNERS];  // NAN when that corner failed or span not set
  float totalKg;                // sum of corners; NAN unless ok && calibrated
};

void calibrationBegin();
bool calibrationIsReady();   // shared span factor is set
bool calibrationHasTare();   // per-corner offsets have been captured
long calibrationOffset(uint8_t corner);
float calibrationScale();    // shared span, raw counts per kg
float calibrationCornerKg(uint8_t corner, long raw);
ScaleReading calibrationReadAll(uint8_t samplesPerCorner);
float calibrationWeightKgMedian(const float *weightsKg, uint8_t count);
// Tare all corners in one pass (platform empty). All-or-nothing: offsets
// are only stored once every corner sampled stably.
bool calibrationTare();
// Shared span from one known weight roughly centered on the platform:
// span = sum(corner deltas) / knownKg. Requires a prior tare.
bool calibrationCalibrate(float knownKg);
void calibrationReset();
void calibrationShow();
