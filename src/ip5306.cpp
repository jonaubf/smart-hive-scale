#include "ip5306.h"

#include <Wire.h>

#include "pins.h"

namespace {

constexpr uint8_t IP5306_ADDR = 0x75;
constexpr uint8_t REG_SYS_CTL0 = 0x00;
constexpr uint8_t BOOST_KEEP_ON_BIT = 0x20;

}  // namespace

void ip5306Begin() { Wire.begin(PIN_PMIC_SDA, PIN_PMIC_SCL); }

bool ip5306SetBoostKeepOn(bool enabled) {
  Wire.beginTransmission(IP5306_ADDR);
  Wire.write(REG_SYS_CTL0);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(IP5306_ADDR, static_cast<uint8_t>(1)) != 1) {
    return false;
  }

  uint8_t data = Wire.read();
  if (enabled) {
    data |= BOOST_KEEP_ON_BIT;
  } else {
    data &= static_cast<uint8_t>(~BOOST_KEEP_ON_BIT);
  }

  Wire.beginTransmission(IP5306_ADDR);
  Wire.write(REG_SYS_CTL0);
  Wire.write(data);
  return Wire.endTransmission() == 0;
}
