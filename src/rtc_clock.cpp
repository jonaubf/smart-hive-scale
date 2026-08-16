#include "rtc_clock.h"

#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <time.h>

#include "pins.h"

namespace {

RTC_DS3231 rtc;
bool present = false;
bool beginAttempted = false;

constexpr uint8_t DS3231_I2C_ADDR = 0x68;
constexpr uint8_t DS3231_REG_CONTROL = 0x0E;

// EOSC (control reg bit 7, active low) stops the oscillator whenever main
// VCC is absent — the clock then silently freezes across every power-off
// despite a good coin cell. Factory default is 0 (runs on battery), but
// clone chips and glitched I2C writes can leave it set, so clear it on
// every boot and verify the write stuck rather than trusting the default.
void ensureOscillatorRunsOnBattery() {
  Wire.beginTransmission(DS3231_I2C_ADDR);
  Wire.write(DS3231_REG_CONTROL);
  if (Wire.endTransmission() != 0 ||
      Wire.requestFrom(DS3231_I2C_ADDR, uint8_t{1}) != 1) {
    Serial.println(F("WARN DS3231 control register read failed — EOSC state unknown"));
    return;
  }
  const uint8_t control = Wire.read();
  if ((control & 0x80) == 0) {
    return;  // oscillator already enabled on battery
  }

  Wire.beginTransmission(DS3231_I2C_ADDR);
  Wire.write(DS3231_REG_CONTROL);
  Wire.write(static_cast<uint8_t>(control & ~0x80));
  Wire.endTransmission();

  Wire.beginTransmission(DS3231_I2C_ADDR);
  Wire.write(DS3231_REG_CONTROL);
  uint8_t readBack = 0x80;
  if (Wire.endTransmission() == 0 &&
      Wire.requestFrom(DS3231_I2C_ADDR, uint8_t{1}) == 1) {
    readBack = Wire.read();
  }
  if ((readBack & 0x80) == 0) {
    Serial.println(F("DS3231: cleared EOSC — oscillator will keep running on coin cell"));
  } else {
    Serial.println(F("WARN DS3231 EOSC clear did not stick — clock will stop without main power"));
  }
}

// Same "is this a real synced time, not the 1970 default" threshold used by
// modemManagerSyncClock() (2023-01-01 UTC) — kept in sync deliberately so
// both modules agree on what counts as plausible.
constexpr time_t PLAUSIBLE_TIME_THRESHOLD = 1672531200;

// Guards against emitting a malformed timestamp from a corrupted read (a
// live I2C transaction can glitch — e.g. during a modem current spike on
// the shared supply — and decode to an out-of-calendar-range field, not
// just a wrong-but-valid-looking one).
bool isPlausibleCalendar(int year, int month, int day, int hour, int minute, int second) {
  return year >= 2023 && year <= 2100 && month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
        hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59;
}

}  // namespace

void rtcClockBegin() {
  if (beginAttempted) {
    return;
  }
  beginAttempted = true;

  present = rtc.begin();
  if (!present) {
    Serial.println(F("ERR DS3231 not found on I2C 0x68"));
    return;
  }

  if (rtc.lostPower()) {
    Serial.println(
        F("WARN DS3231 lost power (missing/dead coin cell?) — time may be wrong until synced"));
  }

  ensureOscillatorRunsOnBattery();

  rtc.clearAlarm(2);
  rtc.disableAlarm(1);  // only Alarm2 is used
}

bool rtcClockIsPresent() { return present; }

bool rtcClockSetNextAlignedAlarm(uint32_t intervalSec) {
  if (!present) {
    return false;
  }

  // Wall-clock-aligned, not "intervalSec from now": e.g. a 1h interval fires
  // at :00 every hour, not at whatever minute the previous cycle happened to
  // finish at. DS3231_A2_Hour only matches hour:minute (no seconds, no
  // date), so the alignment only needs minutes-since-midnight arithmetic —
  // any date works in the DateTime below, it's never compared.
  const DateTime now = rtc.now();
  uint32_t intervalMin = intervalSec / 60;
  if (intervalMin == 0) {
    intervalMin = 1;  // defensive floor; settings never actually go this low
  }
  const uint32_t minutesSinceMidnight = static_cast<uint32_t>(now.hour()) * 60 + now.minute();
  const uint32_t nextBoundary = (minutesSinceMidnight / intervalMin + 1) * intervalMin;
  const uint32_t targetMinuteOfDay = nextBoundary % 1440;
  const uint8_t targetHour = static_cast<uint8_t>(targetMinuteOfDay / 60);
  const uint8_t targetMinute = static_cast<uint8_t>(targetMinuteOfDay % 60);

  const DateTime target(now.year(), now.month(), now.day(), targetHour, targetMinute, 0);
  rtc.clearAlarm(2);
  if (!rtc.setAlarm2(target, DS3231_A2_Hour)) {
    Serial.println(F("ERR DS3231 setAlarm2 failed"));
    return false;
  }
  return true;
}

void rtcClockSyncFromSystemTimeIfNeeded() {
  if (!present) {
    return;
  }
  const time_t systemTime = time(nullptr);
  if (systemTime < PLAUSIBLE_TIME_THRESHOLD) {
    return;  // ESP32 clock hasn't been synced from NITZ/NTP yet — still 1970
  }

  // Calendar-field constructor, not DateTime(uint32_t) — avoids relying on
  // which epoch convention that overload uses in the installed RTClib
  // version. gmtime_r() gives us UTC fields directly from the time_t that
  // modemManagerSyncClock() wrote via settimeofday().
  struct tm parts;
  gmtime_r(&systemTime, &parts);
  rtc.adjust(DateTime(parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday, parts.tm_hour,
                      parts.tm_min, parts.tm_sec));
  Serial.println(F("DS3231 synced from system clock (UTC)"));
}

bool rtcClockSetUtc(int year, int month, int day, int hour, int minute, int second) {
  if (!present) {
    Serial.println(F("ERR DS3231 not present"));
    return false;
  }
  if (!isPlausibleCalendar(year, month, day, hour, minute, second)) {
    Serial.println(F("ERR time out of range (expect UTC, year 2023-2100)"));
    return false;
  }

  rtc.adjust(DateTime(year, month, day, hour, minute, second));

  // Keep the system clock in step so rtcClockNowIso8601()'s no-DS3231 path
  // and any later rtcClockSyncFromSystemTimeIfNeeded() agree with what was
  // just written, instead of pushing a stale 1970 back over it.
  struct tm parts = {};
  parts.tm_year = year - 1900;
  parts.tm_mon = month - 1;
  parts.tm_mday = day;
  parts.tm_hour = hour;
  parts.tm_min = minute;
  parts.tm_sec = second;
  const time_t utc = mktime(&parts);
  struct timeval tv = {.tv_sec = utc, .tv_usec = 0};
  settimeofday(&tv, nullptr);

  Serial.printf("DS3231 set to %04d-%02d-%02d %02d:%02d:%02d UTC\n", year, month, day,
                hour, minute, second);
  return true;
}

void rtcClockPrepareDeepSleepWakeup() {
  if (!present) {
    return;
  }
  const gpio_num_t alarmPin = static_cast<gpio_num_t>(PIN_RTC_ALARM);
  rtc_gpio_deinit(alarmPin);
  rtc_gpio_pullup_en(alarmPin);
  rtc_gpio_pulldown_dis(alarmPin);
  esp_sleep_enable_ext1_wakeup(1ULL << alarmPin, ESP_EXT1_WAKEUP_ALL_LOW);
}

String rtcClockNowIso8601() {
  char buf[21];

  if (present) {
    const DateTime now = rtc.now();
    // Covers both "never synced" (power-on-reset default ~2000) and a
    // corrupted read decoding to an out-of-range field.
    if (!isPlausibleCalendar(now.year(), now.month(), now.day(), now.hour(), now.minute(),
                             now.second())) {
      return String();
    }
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", now.year(), now.month(),
            now.day(), now.hour(), now.minute(), now.second());
    return String(buf);
  }

  const time_t systemTime = time(nullptr);
  if (systemTime < PLAUSIBLE_TIME_THRESHOLD) {
    return String();  // no DS3231, and system clock hasn't been synced either
  }
  struct tm parts;
  gmtime_r(&systemTime, &parts);
  const int year = parts.tm_year + 1900;
  const int month = parts.tm_mon + 1;
  if (!isPlausibleCalendar(year, month, parts.tm_mday, parts.tm_hour, parts.tm_min,
                           parts.tm_sec)) {
    return String();
  }
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", year, month, parts.tm_mday,
          parts.tm_hour, parts.tm_min, parts.tm_sec);
  return String(buf);
}

void rtcClockShow() {
  if (!present) {
    Serial.println(F("DS3231: not found"));
    return;
  }
  const DateTime now = rtc.now();
  Serial.printf("DS3231: %04d-%02d-%02d %02d:%02d:%02d lost_power=%s\n", now.year(), now.month(),
                now.day(), now.hour(), now.minute(), now.second(),
                rtc.lostPower() ? "yes" : "no");
}
