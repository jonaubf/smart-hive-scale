#pragma once

#include <stdint.h>

// IP5306 PMIC on the TTGO T-Call (I2C GPIO 21/22).
// Call ip5306EnsureBoostKeepOn() before deep sleep on battery so the chip
// does not auto-cut the 5 V boost under light load (~45 mA / 32 s).

void ip5306Begin();
bool ip5306SetBoostKeepOn(bool enabled);
// begin + enable keep-on + read-back verify; updates ip5306BoostKeepOnOk().
bool ip5306EnsureBoostKeepOn();
bool ip5306BoostKeepOnOk();
uint8_t ip5306SysCtl0();

// Diagnostic: scan the PMIC I2C bus (GPIO 21/22) for any responding address,
// not just 0x75. Prints results to Serial. Use when the PMIC is reported
// "not found" to tell a dead/misrouted bus apart from a chip at another
// address.
void ip5306ScanBus();
