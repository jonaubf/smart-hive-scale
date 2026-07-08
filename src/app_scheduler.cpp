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

bool appSchedulerRunPublishCycle() {
  for (uint8_t attempt = 1; attempt <= PUBLISH_MAX_ATTEMPTS; attempt++) {
    Serial.printf("Publish attempt %u/%u\n", attempt, PUBLISH_MAX_ATTEMPTS);
    if (mqttClientRunPublishTest(MODEM_NETWORK_TIMEOUT_MS,
                                 MODEM_MQTT_CONNECT_TIMEOUT_MS)) {
      return true;
    }
    if (attempt < PUBLISH_MAX_ATTEMPTS) {
      const unsigned long backoffMs = PUBLISH_RETRY_BASE_DELAY_MS * attempt;
      Serial.printf("Publish failed — retry in %lus\n", backoffMs / 1000UL);
      delay(backoffMs);
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
