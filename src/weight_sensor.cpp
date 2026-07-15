#include "weight_sensor.h"

#include <SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h>
#include <Wire.h>

#include "config.h"
#include "pins.h"

namespace {

constexpr unsigned long NAU7802_READY_TIMEOUT_MS = 1500;
constexpr uint8_t NAU7802_MEDIAN_MAX_SAMPLES = 31;

NAU7802 scale;
bool sensorPresent = false;

bool waitForConversion(unsigned long timeoutMs) {
  const unsigned long deadline = millis() + timeoutMs;
  while (!scale.available()) {
    if (static_cast<long>(millis() - deadline) >= 0) {
      return false;
    }
    delay(1);
  }
  return true;
}

// Registers are retained through power-down, but the analog front end needs
// an internal recalibration after it comes back up.
void powerCycle() {
  scale.powerDown();
  delay(10);
  scale.powerUp();
  scale.calibrateAFE();
  delay(100);
}

bool readSingle(long &rawOut) {
  if (!sensorPresent) {
    return false;
  }
  if (!waitForConversion(NAU7802_READY_TIMEOUT_MS)) {
    // A wedged NAU7802 (I2C glitch mid-conversion) recovers after a power
    // cycle; give it one chance before reporting failure.
    powerCycle();
    if (!waitForConversion(NAU7802_READY_TIMEOUT_MS)) {
      return false;
    }
  }
  rawOut = scale.getReading();
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
  // Wire1 = dedicated bus for the scale ADC (Wire/I2C0 is reserved for IP5306).
  Wire1.begin(PIN_SCALE_I2C_SDA, PIN_SCALE_I2C_SCL);

  sensorPresent = scale.begin(Wire1);
  if (!sensorPresent) {
    Serial.println(F("ERR NAU7802 not found on I2C 0x2A — check wiring"));
    return false;
  }

  // Internal LDO excites the bridge; 3.0 V leaves headroom below the 3.3 V
  // supply. Gain 128 matches the previous HX711 channel-A setup; 10 SPS
  // trades speed for noise.
  scale.setLDO(NAU7802_LDO_3V0);
  scale.setGain(NAU7802_GAIN_128);
  scale.setSampleRate(NAU7802_SPS_10);
  // Re-calibrate the analog front end after changing LDO/gain/sample rate.
  scale.calibrateAFE();

  // The first conversions after power-up/AFE calibration are off and would
  // corrupt averages (the chip is power-cycled by deep sleep).
  long discard = 0;
  for (uint8_t i = 0; i < SCALE_WARMUP_READS; i++) {
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

  long value = 0;
  long long sum = 0;
  for (uint8_t i = 0; i < samples; i++) {
    if (!readSingle(value)) {
      return result;
    }
    sum += value;
  }

  result.raw = static_cast<long>(sum / samples);
  result.ok = true;
  return result;
}

void weightSensorPowerDown() {
  if (!sensorPresent) {
    return;
  }
  // Register-controlled power-down (~200 nA); survives ESP32 deep sleep
  // because the setting lives in the NAU7802, not in a GPIO level.
  scale.powerDown();
}

WeightSensorReading weightSensorReadRawMedian(uint8_t samples, uint8_t warmupReads) {
  WeightSensorReading result{false, 0};

  if (samples == 0 || samples > NAU7802_MEDIAN_MAX_SAMPLES) {
    return result;
  }

  long value = 0;
  for (uint8_t i = 0; i < warmupReads; i++) {
    if (!readSingle(value)) {
      return result;
    }
  }

  long values[NAU7802_MEDIAN_MAX_SAMPLES];
  for (uint8_t i = 0; i < samples; i++) {
    if (!readSingle(values[i])) {
      return result;
    }
  }

  result.raw = medianSorted(values, samples);
  result.ok = true;
  return result;
}
