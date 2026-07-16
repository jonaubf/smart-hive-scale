#include "app_scheduler.h"

#include <Arduino.h>
#include <driver/uart.h>
#include <esp_sleep.h>

#include "config.h"
#include "device_settings.h"
#include "ip5306.h"
#include "modem_manager.h"
#include "mqtt_client.h"
#include "radio_manager.h"
#include "rtc_clock.h"
#include "setup_button.h"
#include "weight_sensor.h"

WakeCause appSchedulerWakeCause() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return WakeCause::Timer;
    case ESP_SLEEP_WAKEUP_EXT0:
      return WakeCause::Button;
    case ESP_SLEEP_WAKEUP_EXT1:
      return WakeCause::RtcAlarm;
    default:
      return WakeCause::PowerOn;
  }
}

const char *appSchedulerWakeCauseName(WakeCause cause) {
  switch (cause) {
    case WakeCause::Timer:
      return "timer";
    case WakeCause::RtcAlarm:
      return "rtc_alarm";
    case WakeCause::Button:
      return "button";
    default:
      return "power_on";
  }
}

namespace {

bool waitForSensorWarmup() {
  const unsigned long deadline = SCALE_THERMAL_WARMUP_MS;
  if (millis() >= deadline) {
    return true;
  }
  Serial.printf("Sensor warm-up: measuring in %lus (setup button aborts)\n",
                (deadline - millis()) / 1000UL);
  while (millis() < deadline) {
    if (setupButtonBreakRequested()) {
      Serial.println(F("Setup button / serial — aborting warm-up"));
      return false;
    }
    delay(50);
  }
  return true;
}

// Shared tail for every path that ends in deep sleep: power-domain config,
// setup-button ext0 wake, flush, and the sleep call itself. Callers arm
// their own timer/ext1 wake sources before calling this.
[[noreturn]] void powerDomainsAndSleep() {
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_ON);

  setupButtonPrepareDeepSleepWakeup();

  Serial.flush();
  uart_wait_tx_idle_polling(UART_NUM_0);
  esp_deep_sleep_start();
  Serial.println(F("ERR deep sleep failed — restarting"));
  delay(500);
  ESP.restart();
}

}  // namespace

bool appSchedulerRunPublishCycle() {
  if (!waitForSensorWarmup()) {
    return false;
  }
  for (uint8_t attempt = 1; attempt <= PUBLISH_MAX_ATTEMPTS; attempt++) {
    Serial.printf("Publish attempt %u/%u\n", attempt, PUBLISH_MAX_ATTEMPTS);
    if (mqttClientRunPublishTest(MODEM_NETWORK_TIMEOUT_MS,
                                 MODEM_MQTT_CONNECT_TIMEOUT_MS)) {
      return true;
    }
    if (setupButtonBreakWasRequested()) {
      Serial.println(F("Publish aborted — entering bench mode"));
      return false;
    }
    if (attempt < PUBLISH_MAX_ATTEMPTS) {
      const unsigned long backoffMs = PUBLISH_RETRY_BASE_DELAY_MS * attempt;
      Serial.printf("Publish failed — retry in %lus (setup button aborts)\n",
                    backoffMs / 1000UL);
      const unsigned long deadline = millis() + backoffMs;
      while (static_cast<long>(millis() - deadline) < 0) {
        if (setupButtonBreakRequested()) {
          Serial.println(F("Setup button / serial — aborting retries"));
          return false;
        }
        delay(50);
      }
    }
  }
  Serial.println(F("ERR publish failed after all attempts — sleeping anyway"));
  return false;
}

void appSchedulerEnterDeepSleep() {
  const unsigned long intervalSec = settingsTxIntervalSec();

  ip5306EnsureBoostKeepOn();

  bool armShortTimer = false;
  if (rtcClockIsPresent()) {
    // Opportunistic: cheap, and picks up a fresh network/NTP-synced system
    // clock whenever one is available (GSM mode syncs it during MQTT
    // connect); no-ops until the system clock itself is plausible.
    rtcClockSyncFromSystemTimeIfNeeded();
    Serial.printf("Entering deep sleep, next report at next %lus wall-clock boundary\n",
                  intervalSec);
    rtcClockSetNextAlignedAlarm(intervalSec);
    armShortTimer = !ip5306BoostKeepOnOk();
    if (armShortTimer) {
      Serial.printf(
          "IP5306 keep-on unverified — also waking every %lus to keep the rail alive\n",
          IP5306_KEEPALIVE_CHUNK_SEC);
    }
  } else {
    Serial.printf(
        "ERR DS3231 not found — falling back to internal timer, next report in %lus "
        "(not wall-clock aligned, no IP5306 keepalive protection)\n",
        intervalSec);
  }

  weightSensorPowerDown();
  modemManagerPowerOff();
  radioDeepSleepPowerDown();

  if (rtcClockIsPresent()) {
    rtcClockPrepareDeepSleepWakeup();
    if (armShortTimer) {
      esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(IP5306_KEEPALIVE_CHUNK_SEC) *
                                    1000000ULL);
    }
  } else {
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(intervalSec) * 1000000ULL);
  }

  powerDomainsAndSleep();
}

void appSchedulerContinueKeepaliveSleep() {
  Serial.println(F("IP5306 keepalive pulse"));
  Serial.flush();
  // Stay awake briefly so bus load / ESP current resets the IP5306 32 s timer.
  delay(500);
  ip5306EnsureBoostKeepOn();  // retry — may recover

  rtcClockPrepareDeepSleepWakeup();  // DS3231 alarm target is untouched — it
                                     // keeps counting down in its own
                                     // hardware regardless of ESP32 sleep state.
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(IP5306_KEEPALIVE_CHUNK_SEC) * 1000000ULL);

  powerDomainsAndSleep();
}
