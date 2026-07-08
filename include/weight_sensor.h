#pragma once

#include <stdint.h>

struct WeightSensorReading {
  bool ok;
  long raw;
};

bool weightSensorBegin();
WeightSensorReading weightSensorReadRaw(uint8_t samples);
WeightSensorReading weightSensorReadRawMedian(uint8_t samples, uint8_t warmupReads);
// Put the HX711 into power-down mode (~1 µA) before deep sleep.
void weightSensorPowerDown();
