#include "ip5306.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

namespace {

constexpr uint8_t IP5306_ADDR = 0x75;
constexpr uint8_t REG_SYS_CTL0 = 0x00;
constexpr uint8_t BOOST_KEEP_ON_BIT = 0x02;
constexpr uint8_t SYS_CTL0_KEEP_ON_LILYGO = 0x37;

TwoWire &pmicWire() { return Wire; }

bool lastKeepOnOk = false;
uint8_t lastSysCtl0 = 0;
int lastWriteError = -1;

bool writeSysCtl0(uint8_t value) {
  pmicWire().beginTransmission(IP5306_ADDR);
  pmicWire().write(REG_SYS_CTL0);
  pmicWire().write(value);
  lastWriteError = pmicWire().endTransmission();
  return lastWriteError == 0;
}

bool readSysCtl0(uint8_t &valueOut) {
  pmicWire().beginTransmission(IP5306_ADDR);
  pmicWire().write(REG_SYS_CTL0);
  if (pmicWire().endTransmission(true) != 0) {
    return false;
  }
  if (pmicWire().requestFrom(static_cast<int>(IP5306_ADDR), 1) != 1) {
    return false;
  }
  valueOut = static_cast<uint8_t>(pmicWire().read());
  return true;
}

bool probePresent() {
  pmicWire().beginTransmission(IP5306_ADDR);
  lastWriteError = pmicWire().endTransmission();
  return lastWriteError == 0;
}

}  // namespace

void ip5306Begin() {
  // Short timeout so a missing IP5306 cannot hang setup long enough
  // to trip the ESP32 task watchdog (that was the reboot loop).
  pmicWire().begin(PIN_PMIC_SDA, PIN_PMIC_SCL, 100000U);
  pmicWire().setTimeOut(50);
}

bool ip5306SetBoostKeepOn(bool enabled) {
  ip5306Begin();

  if (!probePresent()) {
    Serial.printf("ERR IP5306: not found on I2C (err=%d SDA=%d SCL=%d)\n",
                  lastWriteError, PIN_PMIC_SDA, PIN_PMIC_SCL);
    lastKeepOnOk = false;
    return false;
  }

  uint8_t value = enabled ? SYS_CTL0_KEEP_ON_LILYGO
                          : static_cast<uint8_t>(SYS_CTL0_KEEP_ON_LILYGO & ~BOOST_KEEP_ON_BIT);

  uint8_t current = 0;
  if (readSysCtl0(current)) {
    lastSysCtl0 = current;
    value = enabled ? static_cast<uint8_t>(current | BOOST_KEEP_ON_BIT)
                    : static_cast<uint8_t>(current & ~BOOST_KEEP_ON_BIT);
  }

  if (!writeSysCtl0(value)) {
    Serial.printf("ERR IP5306: I2C write failed err=%d\n", lastWriteError);
    lastKeepOnOk = false;
    return false;
  }

  // Read back and verify the bit actually stuck — some IP5306 clones ack the
  // write but silently ignore it (or revert it once the bus goes idle), and
  // the deep-sleep auto-shutoff only stops if the bit is really set. Trusting
  // a successful I2C ack alone previously reported false positives, which let
  // devices skip the app_scheduler keepalive-chunk fallback and die on
  // battery after ~32s.
  uint8_t readBack = 0;
  if (!readSysCtl0(readBack)) {
    Serial.println(F("ERR IP5306: read-back failed after write"));
    lastKeepOnOk = false;
    return false;
  }
  lastSysCtl0 = readBack;

  const bool bitSet = (readBack & BOOST_KEEP_ON_BIT) != 0;
  lastKeepOnOk = (bitSet == enabled);
  if (!lastKeepOnOk) {
    Serial.printf("ERR IP5306: keep-on bit did not stick (wrote 0x%02X, read 0x%02X)\n",
                  value, readBack);
  }
  return lastKeepOnOk;
}

bool ip5306EnsureBoostKeepOn() {
  const bool ok = ip5306SetBoostKeepOn(true);
  if (ok) {
    Serial.printf("IP5306 boost keep-on OK (SYS_CTL0=0x%02X)\n", lastSysCtl0);
  } else {
    Serial.println(F("ERR IP5306 boost keep-on FAILED — using 25s sleep chunks"));
  }
  Serial.flush();
  return ok;
}

bool ip5306BoostKeepOnOk() { return lastKeepOnOk; }

uint8_t ip5306SysCtl0() { return lastSysCtl0; }

void ip5306ScanBus() {
  ip5306Begin();
  Serial.printf("IP5306 bus scan (SDA=%d SCL=%d)...\n", PIN_PMIC_SDA, PIN_PMIC_SCL);
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    pmicWire().beginTransmission(addr);
    if (pmicWire().endTransmission() == 0) {
      Serial.printf("  found device at 0x%02X%s\n", addr,
                    addr == IP5306_ADDR ? " (expected IP5306 address)" : "");
      found++;
    }
  }
  if (found == 0) {
    Serial.println(F("  no devices responded — bus is dead (wiring/power), not just wrong address"));
  }
  Serial.printf("Scan done: %u device(s)\n", found);
}
