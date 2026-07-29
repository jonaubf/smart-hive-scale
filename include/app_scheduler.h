#pragma once

enum class WakeCause {
  PowerOn,   // cold boot, reset, reflash
  Timer,     // ESP32 internal RTC timer — only armed in the DS3231-absent
             // fallback, where it IS the scheduled report signal
  RtcAlarm,  // DS3231 alarm (ext1) — scheduled report is due
  Button,    // setup button pressed during deep sleep
};

WakeCause appSchedulerWakeCause();
const char *appSchedulerWakeCauseName(WakeCause cause);

// Measure + connect + publish with retries and backoff (FR-5).
// Returns true when the publish succeeded.
bool appSchedulerRunPublishCycle();

// Programs the next DS3231 report alarm (if present), powers down
// modem/WiFi/NAU7802, latches the modem rail enable low through sleep, and
// enters deep sleep until the next report or setup button press.
// Does not return.
void appSchedulerEnterDeepSleep();
