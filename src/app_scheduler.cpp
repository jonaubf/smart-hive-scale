#include "app_scheduler.h"

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "config.h"
#include "connectivity_mode.h"
#include "device_settings.h"
#include "modem_manager.h"
#include "mqtt_client.h"
#include "pins.h"
#include "radio_manager.h"
#include "setup_button.h"
#include "weight_sensor.h"
#include "wifi_manager.h"

WakeCause appSchedulerWakeCause() {
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_TIMER:
      return WakeCause::Timer;
    case ESP_SLEEP_WAKEUP_EXT0:
      return WakeCause::Button;
    default:
      return WakeCause::PowerOn;
  }
}

const char *appSchedulerWakeCauseName(WakeCause cause) {
  switch (cause) {
    case WakeCause::Timer:
      return "timer";
    case WakeCause::Button:
      return "button";
    default:
      return "power_on";
  }
}

namespace {

// Wait until the board has been powered HX711_THERMAL_WARMUP_MS so the
// load cell reading is taken at a consistent thermal state. No-op when the
// device has already been awake that long (e.g. after the bench window).
bool waitForSensorWarmup() {
  const unsigned long deadline = HX711_THERMAL_WARMUP_MS;
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
  Serial.printf("Entering deep sleep for %lus (press setup button to wake)\n",
                intervalSec);

  weightSensorPowerDown();
  modemManagerPowerOff();
  if (connectivityMode() == ConnectivityMode::WifiSta) {
    wifiManagerDisconnect();
  }
  radioPowerDown();

  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(intervalSec) * 1000000ULL);

  // Setup button (GPIO 13 is RTC-capable): wake on press for maintenance.
  const gpio_num_t buttonPin = static_cast<gpio_num_t>(PIN_SETUP_BUTTON);
  rtc_gpio_pullup_en(buttonPin);
  rtc_gpio_pulldown_dis(buttonPin);
  esp_sleep_enable_ext0_wakeup(buttonPin, 0);

  Serial.flush();
  esp_deep_sleep_start();
  // esp_deep_sleep_start() should not return
  Serial.println(F("ERR deep sleep failed — restarting"));
  delay(500);
  ESP.restart();
}
