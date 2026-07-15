#pragma once

enum class WakeCause {
  PowerOn,   // cold boot, reset, reflash
  Timer,     // ESP32 internal RTC timer — IP5306 keepalive pulse, or (DS3231
             // absent fallback only) the scheduled report itself
  RtcAlarm,  // DS3231 alarm (ext1) — scheduled report is due
  Button,    // setup button pressed during deep sleep
};

WakeCause appSchedulerWakeCause();
const char *appSchedulerWakeCauseName(WakeCause cause);

// Measure + connect + publish with retries and backoff (FR-5).
// Returns true when the publish succeeded.
bool appSchedulerRunPublishCycle();

// Programs the next DS3231 report alarm (if present), powers down
// modem/WiFi/NAU7802, and enters deep sleep until the next report, setup
// button press, or (if the PMIC keep-on isn't verified) keepalive pulse.
// Does not return.
void appSchedulerEnterDeepSleep();

// IP5306 keepalive-only continuation: re-arms the short wake window without
// touching the DS3231 report alarm, then sleeps again. Only call when
// rtcClockIsPresent() — a Timer wake with no DS3231 present is the
// scheduled report itself, not a keepalive pulse. Does not return.
void appSchedulerContinueKeepaliveSleep();
