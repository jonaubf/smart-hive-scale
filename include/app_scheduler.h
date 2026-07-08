#pragma once

enum class WakeCause {
  PowerOn,  // cold boot, reset, reflash
  Timer,    // RTC timer — scheduled report
  Button,   // setup button pressed during deep sleep
};

WakeCause appSchedulerWakeCause();
const char *appSchedulerWakeCauseName(WakeCause cause);

// Measure + connect + publish with retries and backoff (FR-5).
// Returns true when the publish succeeded.
bool appSchedulerRunPublishCycle();

// Power down modem/WiFi/HX711 and enter deep sleep until the next
// tx interval (or setup button press). Does not return.
void appSchedulerEnterDeepSleep();
