#include "rtc_clock.h"

#include <Arduino.h>
#include <RTClib.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <time.h>

#include "pins.h"

namespace {

RTC_DS3231 rtc;
bool present = false;
bool beginAttempted = false;

// Same "is this a real synced time, not the 1970 default" threshold used by
// modemManagerSyncClock() (2023-01-01 UTC) — kept in sync deliberately so
// both modules agree on what counts as plausible.
constexpr time_t PLAUSIBLE_TIME_THRESHOLD = 1672531200;

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

  rtc.clearAlarm(2);
  rtc.disableAlarm(1);  // only Alarm2 is used
}

bool rtcClockIsPresent() { return present; }

bool rtcClockSetAlarmIn(uint32_t secondsFromNow) {
  if (!present) {
    return false;
  }

  const DateTime target = rtc.now() + TimeSpan(static_cast<int32_t>(secondsFromNow));
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
    if (now.year() < 2023) {
      return String();  // never synced — still the power-on-reset default (~2000)
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
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", parts.tm_year + 1900,
          parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec);
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
