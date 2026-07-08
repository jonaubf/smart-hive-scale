#pragma once

#include <stdint.h>

void calibrationBegin();
bool calibrationIsReady();
long calibrationOffset();
float calibrationScale();
float calibrationWeightKg(long raw);
float calibrationWeightKgMedian(const float *weightsKg, uint8_t count);
bool calibrationTare();
bool calibrationCalibrate(float knownKg);
void calibrationReset();
void calibrationShow();
