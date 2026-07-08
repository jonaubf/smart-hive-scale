#include "device_settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include "config.h"

namespace {

constexpr const char *NVS_NAMESPACE = "beekpr_cfg";
constexpr const char *KEY_TX_INTERVAL = "tx_int_s";
constexpr uint16_t MIN_INTERVAL_MIN = 1;
constexpr uint16_t MAX_INTERVAL_MIN = 1440;

Preferences prefs;
unsigned long txIntervalSec = WAKE_INTERVAL_SEC;

}  // namespace

void settingsBegin() {
  prefs.begin(NVS_NAMESPACE, false);
  txIntervalSec = prefs.getULong(KEY_TX_INTERVAL, WAKE_INTERVAL_SEC);
}

unsigned long settingsTxIntervalSec() { return txIntervalSec; }

bool settingsSetTxIntervalMin(uint16_t minutes) {
  if (minutes < MIN_INTERVAL_MIN || minutes > MAX_INTERVAL_MIN) {
    return false;
  }

  txIntervalSec = static_cast<unsigned long>(minutes) * 60UL;
  prefs.putULong(KEY_TX_INTERVAL, txIntervalSec);
  return true;
}

void settingsShow() {
  Serial.printf("tx_interval_sec=%lu tx_interval_min=%.2f\n", txIntervalSec,
                txIntervalSec / 60.0f);
}
