#include "weight_sensor.h"

#include <HX711.h>

#include "config.h"
#include "pins.h"

namespace {

constexpr uint8_t HX711_GAIN = 128;
constexpr unsigned long HX711_READY_TIMEOUT_MS = 1500;
constexpr uint8_t HX711_MEDIAN_MAX_SAMPLES = 31;

HX711 scale;

bool readSingle(long &rawOut) {
  if (!scale.wait_ready_timeout(HX711_READY_TIMEOUT_MS)) {
    return false;
  }
  rawOut = scale.read();
  return true;
}

long medianSorted(long *values, uint8_t count) {
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

}  // namespace

bool weightSensorBegin() {
  scale.begin(PIN_HX711_DT, PIN_HX711_SCK);
  scale.set_gain(HX711_GAIN);

  // HX711 output only settles ~400 ms after power-up (it is power-cycled by
  // deep sleep); the first conversions are off and would corrupt averages.
  long discard = 0;
  for (uint8_t i = 0; i < HX711_WARMUP_READS; i++) {
    if (!readSingle(discard)) {
      break;
    }
  }
  return true;
}

WeightSensorReading weightSensorReadRaw(uint8_t samples) {
  WeightSensorReading result{false, 0};

  if (samples == 0) {
    samples = 1;
  }

  if (!readSingle(result.raw)) {
    return result;
  }

  if (samples == 1) {
    result.ok = true;
    return result;
  }

  result.raw = scale.read_average(samples);
  result.ok = true;
  return result;
}

// Note: SCK is GPIO12, an ESP32 strapping pin (flash voltage). Do NOT hold it
// high across deep sleep — a high level at wake reset can break booting.
void weightSensorPowerDown() { scale.power_down(); }

WeightSensorReading weightSensorReadRawMedian(uint8_t samples, uint8_t warmupReads) {
  WeightSensorReading result{false, 0};

  if (samples == 0 || samples > HX711_MEDIAN_MAX_SAMPLES) {
    return result;
  }

  long value = 0;
  for (uint8_t i = 0; i < warmupReads; i++) {
    if (!readSingle(value)) {
      return result;
    }
  }

  long values[HX711_MEDIAN_MAX_SAMPLES];
  for (uint8_t i = 0; i < samples; i++) {
    if (!readSingle(values[i])) {
      return result;
    }
  }

  result.raw = medianSorted(values, samples);
  result.ok = true;
  return result;
}
