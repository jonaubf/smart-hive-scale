#pragma once

#include <stdint.h>

struct WeightSensorReading {
  bool ok;
  long raw;
};

// 4 NAU7802s (one per hive-stand corner), each behind its own PCA9548A mux
// channel — corner N maps to CORNER_MUX_CHANNEL[N] (config.h). Requires Wire
// and i2cMuxBegin() done first (main.cpp setup()).

// Begin every corner. Returns true if at least one corner responds; check
// weightSensorCornerPresent() for the per-corner outcome (FR-11: one dead
// corner degrades to a flagged outlier, not a failed device).
bool weightSensorBegin();
bool weightSensorCornerPresent(uint8_t corner);

// Average of `samples` conversions from one corner.
WeightSensorReading weightSensorReadCornerRaw(uint8_t corner, uint8_t samples);

// Put every present NAU7802 into power-down mode (~200 nA) before deep
// sleep; the setting lives in each chip's registers, not a GPIO level.
void weightSensorPowerDown();
