#include "calibration.h"

#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

#include "config.h"
#include "weight_sensor.h"

namespace {

constexpr const char *NVS_NAMESPACE = "beekpr";
// Keys are per-corner ("off0".."off3") + one shared span. The old
// single-cell keys ("offset"/"scale") are ignored — new hardware, fresh
// calibration; calibrationReset() clears them too so no stale data lingers.
constexpr const char *KEY_SPAN = "span";
constexpr const char *KEY_TARED = "tared";
constexpr const char *KEY_OFFSET_PREFIX = "off";
constexpr long ZERO_DRIFT_WARN_COUNTS = 800;
constexpr long TARE_MAX_SPREAD_COUNTS = 500;

Preferences prefs;
long offsets[NUM_CORNERS] = {};
float span = 0.0f;
bool tared = false;
bool calibrated = false;

void offsetKey(uint8_t corner, char *out, size_t outLen) {
  snprintf(out, outLen, "%s%u", KEY_OFFSET_PREFIX, corner);
}

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

// Single pass on one corner: settle briefly, then SCALE_CAL_SAMPLES
// consecutive conversions (~2 s at 10 SPS) and their median.
StableSampleResult readStableSamples(uint8_t corner) {
  StableSampleResult result{false, 0, 0};

  delay(SCALE_CAL_SETTLE_MS);

  long values[SCALE_CAL_SAMPLES];
  for (uint8_t i = 0; i < SCALE_CAL_SAMPLES; i++) {
    const WeightSensorReading reading = weightSensorReadCornerRaw(corner, 1);
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
  const ScaleReading reading = calibrationReadAll(SCALE_RAW_SAMPLES);
  for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
    if (!reading.cornerOk[corner]) {
      Serial.printf("verify c%u=not_ready\n", corner);
      continue;
    }
    const long delta = reading.cornerRaw[corner] - offsets[corner];
    Serial.printf("verify c%u raw=%ld delta=%ld\n", corner, reading.cornerRaw[corner],
                  delta);
    if (labs(delta) > ZERO_DRIFT_WARN_COUNTS) {
      Serial.printf("WARN c%u zero drift — keep still and run tare again\n", corner);
    }
  }
}

}  // namespace

void calibrationBegin() {
  prefs.begin(NVS_NAMESPACE, false);
  char key[8];
  for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
    offsetKey(corner, key, sizeof(key));
    offsets[corner] = prefs.getLong(key, 0);
  }
  span = prefs.getFloat(KEY_SPAN, 0.0f);
  tared = prefs.getBool(KEY_TARED, false);
  calibrated = span != 0.0f;
}

bool calibrationIsReady() { return calibrated; }

bool calibrationHasTare() { return tared; }

long calibrationOffset(uint8_t corner) {
  return corner < NUM_CORNERS ? offsets[corner] : 0;
}

float calibrationScale() { return span; }

float calibrationCornerKg(uint8_t corner, long raw) {
  if (!calibrated || corner >= NUM_CORNERS) {
    return NAN;
  }
  return static_cast<float>(raw - offsets[corner]) / span;
}

ScaleReading calibrationReadAll(uint8_t samplesPerCorner) {
  ScaleReading result{};
  result.ok = true;
  result.totalKg = NAN;

  float sumKg = 0.0f;
  for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
    const WeightSensorReading reading =
        weightSensorReadCornerRaw(corner, samplesPerCorner);
    result.cornerOk[corner] = reading.ok;
    result.cornerRaw[corner] = reading.raw;
    result.cornerKg[corner] = reading.ok ? calibrationCornerKg(corner, reading.raw) : NAN;
    if (!reading.ok) {
      result.ok = false;
    } else if (calibrated) {
      sumKg += result.cornerKg[corner];
    }
  }

  // A total with a corner missing would silently under-read — only report
  // one when every corner contributed (FR-11: the bad corner shows up as
  // NAN/outlier instead of skewing a blended number).
  if (result.ok && calibrated) {
    result.totalKg = sumKg;
  }
  return result;
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
  Serial.printf("Tare: median of %u samples per corner...\n", SCALE_CAL_SAMPLES);

  // Sample every corner first, store nothing until all 4 pass — a partial
  // tare (2 fresh offsets + 2 stale) would corrupt every total afterwards.
  long medians[NUM_CORNERS];
  for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
    const StableSampleResult sample = readStableSamples(corner);
    if (!sample.ok) {
      Serial.printf("ERR tare c%u not ready\n", corner);
      return false;
    }
    Serial.printf("c%u spread=%ld counts\n", corner, sample.spread);
    if (sample.spread > TARE_MAX_SPREAD_COUNTS) {
      Serial.printf("ERR tare c%u unstable — keep still and retry\n", corner);
      return false;
    }
    medians[corner] = sample.median;
  }

  char key[8];
  for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
    offsets[corner] = medians[corner];
    offsetKey(corner, key, sizeof(key));
    prefs.putLong(key, offsets[corner]);
  }
  tared = true;
  prefs.putBool(KEY_TARED, true);
  printVerifyTare();
  return true;
}

bool calibrationCalibrate(float knownKg) {
  if (knownKg <= 0.0f || !tared) {
    return false;
  }

  Serial.printf("Cal: median of %u samples per corner...\n", SCALE_CAL_SAMPLES);

  long sumDelta = 0;
  for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
    const StableSampleResult sample = readStableSamples(corner);
    if (!sample.ok) {
      Serial.printf("ERR cal c%u not ready\n", corner);
      return false;
    }
    const long delta = sample.median - offsets[corner];
    const long allowedSpread = maxAllowedSpread(delta);
    Serial.printf("c%u delta=%ld spread=%ld counts (max %ld)\n", corner, delta,
                  sample.spread, allowedSpread);
    if (sample.spread > allowedSpread) {
      Serial.printf("ERR cal c%u unstable — keep weight still and retry\n", corner);
      return false;
    }
    sumDelta += delta;
  }

  if (sumDelta == 0) {
    Serial.println(F("ERR cal failed — no change from tare offsets"));
    return false;
  }

  // One shared factor from the summed delta: only assumes the *sum* tracks
  // weight linearly — not that the frame loads each corner exactly 1/4.
  span = static_cast<float>(sumDelta) / knownKg;
  prefs.putFloat(KEY_SPAN, span);
  calibrated = true;

  const float measuredKg = static_cast<float>(sumDelta) / span;
  Serial.printf("verify sum_delta=%ld weight_kg=%.3f (expected %.3f)\n", sumDelta,
                measuredKg, knownKg);
  return true;
}

void calibrationReset() {
  char key[8];
  for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
    offsets[corner] = 0;
    offsetKey(corner, key, sizeof(key));
    prefs.remove(key);
  }
  span = 0.0f;
  tared = false;
  calibrated = false;
  prefs.remove(KEY_SPAN);
  prefs.remove(KEY_TARED);
  // Also clear the retired single-cell keys so old boards migrate clean.
  prefs.remove("offset");
  prefs.remove("scale");
  Serial.println(F("OK calibration cleared"));
}

void calibrationShow() {
  Serial.print(F("offsets"));
  for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
    Serial.printf(" c%u=%ld", corner, offsets[corner]);
  }
  Serial.println();
  Serial.printf("span=%.3f tared=%s calibrated=%s\n", span, tared ? "yes" : "no",
                calibrated ? "yes" : "no");
}
