#pragma once

#include <Arduino.h>
#include <stdint.h>

// DS3231 precision RTC on the shared IP5306 I2C bus (GPIO 21/22), address
// 0x68. Drives the report schedule via its alarm output (PIN_RTC_ALARM,
// ext1 deep-sleep wake) so reports land on precise wall-clock time instead
// of drifting on the ESP32's internal RC-oscillator timer.

// Idempotent. Requires Wire already begun on the IP5306 pins — call after
// ip5306EnsureBoostKeepOn().
void rtcClockBegin();
bool rtcClockIsPresent();

// Programs Alarm2 (minute precision) to fire secondsFromNow from the
// DS3231's own current time. Returns false if not present.
bool rtcClockSetAlarmIn(uint32_t secondsFromNow);

// Writes the ESP32 system clock (UTC) into the DS3231, but only once the
// system clock itself looks plausible (post 2023-01-01) — modemManagerSyncClock()
// sets it from NITZ/NTP over GPRS after a successful GSM connect; until that
// has happened the system clock still reads 1970 and must not be trusted.
// No-op if not present or the system clock isn't plausible yet.
void rtcClockSyncFromSystemTimeIfNeeded();

// Configures ext1 deep-sleep wake on PIN_RTC_ALARM. No-op if not present.
void rtcClockPrepareDeepSleepWakeup();

void rtcClockShow();

// ISO8601 UTC timestamp ("YYYY-MM-DDTHH:MM:SSZ") from the most authoritative
// clock available: the DS3231 if present, otherwise the ESP32 system clock.
// Returns an empty string if neither has ever been synced (still at its
// power-on-reset default) — callers should treat that the same as any other
// unavailable reading (e.g. emit JSON null, matching temp_scale_c's NAN).
String rtcClockNowIso8601();
