#pragma once

#include <stdint.h>

struct WeightSensorReading {
  bool ok;
  long raw;
};

bool weightSensorBegin();
WeightSensorReading weightSensorReadRaw(uint8_t samples);
WeightSensorReading weightSensorReadRawMedian(uint8_t samples, uint8_t warmupReads);
// Put the NAU7802 into power-down mode (~200 nA) before deep sleep.
void weightSensorPowerDown();
