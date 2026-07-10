#include "calibration.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

#include "config.h"
#include "weight_sensor.h"

namespace {

constexpr const char *NVS_NAMESPACE = "beekpr";
constexpr const char *KEY_OFFSET = "offset";
constexpr const char *KEY_SCALE = "scale";
constexpr long ZERO_DRIFT_WARN_COUNTS = 800;
constexpr long TARE_MAX_SPREAD_COUNTS = 500;

Preferences prefs;
long offset = 0;
float scale = 0.0f;
bool calibrated = false;

struct StableSampleResult {
  bool ok;
  long median;
  long spread;
};

long medianInPlace(long *values, uint8_t count) {
  for (uint8_t i = 1; i < count; i++) {
    const long key = values[i];
    int j = static_cast<int>(i) - 1;
    while (j >= 0 && values[j] > key) {
      values[j + 1] = values[j];
      j--;
    }
    values[j + 1] = key;
  }
  return values[count / 2];
}

long sampleSpread(long *values, uint8_t count) {
  long minValue = values[0];
  long maxValue = values[0];
  for (uint8_t i = 1; i < count; i++) {
    if (values[i] < minValue) {
      minValue = values[i];
    }
    if (values[i] > maxValue) {
      maxValue = values[i];
    }
  }
  return maxValue - minValue;
}

// Single pass: settle briefly, then take SCALE_CAL_SAMPLES consecutive
// conversions (~2 s at 10 SPS) and use their median.
StableSampleResult readStableSamples() {
  StableSampleResult result{false, 0, 0};

  delay(SCALE_CAL_SETTLE_MS);

  long values[SCALE_CAL_SAMPLES];
  for (uint8_t i = 0; i < SCALE_CAL_SAMPLES; i++) {
    WeightSensorReading reading = weightSensorReadRaw(1);
    if (!reading.ok) {
      return result;
    }
    values[i] = reading.raw;
  }

  result.ok = true;
  result.median = medianInPlace(values, SCALE_CAL_SAMPLES);
  result.spread = sampleSpread(values, SCALE_CAL_SAMPLES);
  return result;
}

long maxAllowedSpread(long deltaFromOffset) {
  const long relativeLimit = labs(deltaFromOffset) / 200;
  return relativeLimit > TARE_MAX_SPREAD_COUNTS ? relativeLimit : TARE_MAX_SPREAD_COUNTS;
}

void printVerifyTare() {
  delay(SCALE_READ_INTERVAL_MS);
  WeightSensorReading reading = weightSensorReadRaw(SCALE_RAW_SAMPLES);
  if (!reading.ok) {
    Serial.println(F("verify=not_ready"));
    return;
  }

  const long delta = reading.raw - offset;
  if (calibrated) {
    Serial.printf("verify raw=%ld weight_kg=%.3f\n", reading.raw,
                  static_cast<float>(delta) / scale);
  } else {
    Serial.printf("verify raw=%ld delta=%ld\n", reading.raw, delta);
  }

  if (labs(delta) > ZERO_DRIFT_WARN_COUNTS) {
    Serial.println(F("WARN zero drift — keep still and run tare again"));
  }
}

void printVerifyCal(float knownKg, long loadedRaw) {
  const long delta = loadedRaw - offset;
  const float measuredKg = static_cast<float>(delta) / scale;
  const float errorKg = measuredKg - knownKg;
  Serial.printf("verify loaded raw=%ld weight_kg=%.3f (expected %.3f, error %+.3f)\n",
                loadedRaw, measuredKg, knownKg, errorKg);
}

}  // namespace

void calibrationBegin() {
  prefs.begin(NVS_NAMESPACE, false);
  offset = prefs.getLong(KEY_OFFSET, 0);
  scale = prefs.getFloat(KEY_SCALE, 0.0f);
  calibrated = scale != 0.0f;
}

bool calibrationIsReady() { return calibrated; }

long calibrationOffset() { return offset; }

float calibrationScale() { return scale; }

float calibrationWeightKg(long raw) {
  if (!calibrated) {
    return NAN;
  }
  return static_cast<float>(raw - offset) / scale;
}

float calibrationWeightKgMedian(const float *weightsKg, uint8_t count) {
  if (!calibrated || count == 0) {
    return NAN;
  }

  float sorted[SCALE_DISPLAY_MEDIAN_COUNT];
  for (uint8_t i = 0; i < count; i++) {
    sorted[i] = weightsKg[i];
  }

  for (uint8_t i = 1; i < count; i++) {
    const float key = sorted[i];
    int j = static_cast<int>(i) - 1;
    while (j >= 0 && sorted[j] > key) {
      sorted[j + 1] = sorted[j];
      j--;
    }
    sorted[j + 1] = key;
  }

  return sorted[count / 2];
}

bool calibrationTare() {
  Serial.printf("Tare: median of %u samples...\n", SCALE_CAL_SAMPLES);

  StableSampleResult sample = readStableSamples();
  if (!sample.ok) {
    return false;
  }

  Serial.printf("samples spread=%ld counts\n", sample.spread);
  if (sample.spread > TARE_MAX_SPREAD_COUNTS) {
    Serial.println(F("ERR tare unstable — keep still and retry"));
    return false;
  }

  offset = sample.median;
  prefs.putLong(KEY_OFFSET, offset);
  printVerifyTare();
  return true;
}

bool calibrationCalibrate(float knownKg) {
  if (knownKg <= 0.0f) {
    return false;
  }

  Serial.printf("Cal: median of %u samples...\n", SCALE_CAL_SAMPLES);

  StableSampleResult sample = readStableSamples();
  if (!sample.ok) {
    return false;
  }

  const long delta = sample.median - offset;
  if (delta == 0) {
    Serial.println(F("ERR cal failed — no change from tare offset"));
    return false;
  }

  const long allowedSpread = maxAllowedSpread(delta);
  Serial.printf("samples spread=%ld counts (max %ld)\n", sample.spread, allowedSpread);
  if (sample.spread > allowedSpread) {
    Serial.println(F("ERR cal unstable — keep weight still and retry"));
    return false;
  }

  scale = static_cast<float>(delta) / knownKg;
  prefs.putFloat(KEY_SCALE, scale);
  calibrated = true;
  printVerifyCal(knownKg, sample.median);
  return true;
}

void calibrationReset() {
  offset = 0;
  scale = 0.0f;
  calibrated = false;
  prefs.remove(KEY_OFFSET);
  prefs.remove(KEY_SCALE);
  Serial.println(F("OK calibration cleared"));
}

void calibrationShow() {
  Serial.printf("offset=%ld scale=%.3f calibrated=%s\n", offset, scale,
                calibrated ? "yes" : "no");
}
