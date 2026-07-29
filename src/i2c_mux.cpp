#include "i2c_mux.h"

#include <Arduino.h>
#include <Wire.h>

#include "config.h"

namespace {

bool muxPresent = false;

bool writeControl(uint8_t mask) {
  Wire.beginTransmission(I2C_MUX_ADDR);
  Wire.write(mask);
  return Wire.endTransmission() == 0;
}

}  // namespace

bool i2cMuxBegin() {
  // Probe with a harmless "all channels off" write.
  muxPresent = writeControl(0x00);
  if (!muxPresent) {
    Serial.printf("ERR PCA9548A not found at 0x%02X — check wiring\n", I2C_MUX_ADDR);
  }
  return muxPresent;
}

bool i2cMuxIsPresent() { return muxPresent; }

bool i2cMuxSelect(uint8_t channel) {
  if (!muxPresent || channel > 7) {
    return false;
  }
  return writeControl(static_cast<uint8_t>(1U << channel));
}

bool i2cMuxDeselectAll() {
  if (!muxPresent) {
    return false;
  }
  return writeControl(0x00);
}
