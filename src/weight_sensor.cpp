#include "weight_sensor.h"

#include <SparkFun_Qwiic_Scale_NAU7802_Arduino_Library.h>
#include <Wire.h>

#include "config.h"
#include "i2c_mux.h"
#include "pins.h"

namespace {

constexpr unsigned long NAU7802_READY_TIMEOUT_MS = 1500;

// One driver object serves all 4 chips: they are configured identically and
// the object holds no per-chip state — the mux decides which physical
// NAU7802 each I2C transaction reaches.
NAU7802 scale;
bool cornerPresent[NUM_CORNERS] = {};
bool anyPresent = false;

bool selectCorner(uint8_t corner) {
  if (corner >= NUM_CORNERS) {
    return false;
  }
  return i2cMuxSelect(corner);
}

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

// Read one conversion from the currently-selected corner.
bool readSingleOnSelected(uint8_t corner, long &rawOut) {
  if (!cornerPresent[corner]) {
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

bool beginCorner(uint8_t corner) {
  if (!selectCorner(corner)) {
    return false;
  }
  if (!scale.begin(Wire)) {
    return false;
  }

  // Internal LDO excites the bridge; 3.0 V leaves headroom below the 3.3 V
  // supply. Gain 128, 10 SPS trades speed for noise.
  scale.setLDO(NAU7802_LDO_3V0);
  scale.setGain(NAU7802_GAIN_128);
  scale.setSampleRate(NAU7802_SPS_10);
  // Re-calibrate the analog front end after changing LDO/gain/sample rate.
  scale.calibrateAFE();

  cornerPresent[corner] = true;

  // The first conversions after power-up/AFE calibration are off and would
  // corrupt averages (the chip is power-cycled by deep sleep).
  long discard = 0;
  for (uint8_t i = 0; i < SCALE_WARMUP_READS; i++) {
    if (!readSingleOnSelected(corner, discard)) {
      break;
    }
  }
  return true;
}

}  // namespace

bool weightSensorBegin() {
  anyPresent = false;
  for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
    cornerPresent[corner] = false;
    if (beginCorner(corner)) {
      anyPresent = true;
    } else {
      Serial.printf("ERR NAU7802 corner %u not found (mux channel %u, I2C 0x2A)\n",
                    corner, corner);
    }
  }
  i2cMuxDeselectAll();
  return anyPresent;
}

bool weightSensorCornerPresent(uint8_t corner) {
  return corner < NUM_CORNERS && cornerPresent[corner];
}

WeightSensorReading weightSensorReadCornerRaw(uint8_t corner, uint8_t samples) {
  WeightSensorReading result{false, 0};

  if (corner >= NUM_CORNERS || !cornerPresent[corner] || !selectCorner(corner)) {
    return result;
  }
  if (samples == 0) {
    samples = 1;
  }

  long value = 0;
  long long sum = 0;
  for (uint8_t i = 0; i < samples; i++) {
    if (!readSingleOnSelected(corner, value)) {
      i2cMuxDeselectAll();
      return result;
    }
    sum += value;
  }
  i2cMuxDeselectAll();

  result.raw = static_cast<long>(sum / samples);
  result.ok = true;
  return result;
}

void weightSensorPowerDown() {
  for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
    if (!cornerPresent[corner] || !selectCorner(corner)) {
      continue;
    }
    scale.powerDown();
  }
  i2cMuxDeselectAll();
}
