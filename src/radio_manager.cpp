#include "radio_manager.h"

#include <WiFi.h>
#include <esp_wifi.h>

void radioPowerDown() {
  WiFi.mode(WIFI_OFF);
  // Do not call esp_wifi_stop() — it tears down the lwIP tcpip mbox and later
  // WiFi.softAP() (config portal in GSM mode) crashes with "Invalid mbox".
}

void radioDeepSleepPowerDown() {
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect(true, false);
    delay(100);
    esp_wifi_stop();
    delay(50);
  }
  WiFi.mode(WIFI_OFF);
}

void radioPowerUpForPortal() {
  WiFi.persistent(false);
  if (WiFi.getMode() == WIFI_OFF) {
    // First use after radioPowerDown() — mode() initializes the WiFi driver.
    WiFi.mode(WIFI_OFF);
  }
  esp_wifi_start();
}
