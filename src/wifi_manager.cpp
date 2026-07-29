#include "wifi_manager.h"

#include <WiFi.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "config.h"
#include "connectivity_mode.h"
#include "mqtt_settings.h"

namespace {

constexpr unsigned long RECONNECT_INTERVAL_MS = 30000;
constexpr int RSSI_DISCONNECTED = -127;

char hostname[48] = "";
char ipAddress[16] = "";
unsigned long lastReconnectAttemptMs = 0;
bool started = false;

void refreshIpCache() {
  if (WiFi.status() == WL_CONNECTED) {
    strncpy(ipAddress, WiFi.localIP().toString().c_str(), sizeof(ipAddress) - 1);
    ipAddress[sizeof(ipAddress) - 1] = '\0';
    return;
  }
  ipAddress[0] = '\0';
}

}  // namespace

void wifiManagerBegin() {
  snprintf(hostname, sizeof(hostname), "beekpr-%s", DEVICE_ID);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);
  started = true;
}

bool wifiManagerConnect(unsigned long timeoutMs) {
  if (!started) {
    wifiManagerBegin();
  }
  if (!connectivityHasWifiCredentials()) {
    Serial.println(F("ERR wifi credentials not configured"));
    return false;
  }
  if (WiFi.status() == WL_CONNECTED) {
    refreshIpCache();
    return true;
  }

  Serial.printf("WiFi connecting to %s as %s...\n", connectivityWifiSsid(), hostname);
  WiFi.begin(connectivityWifiSsid(), connectivityWifiPassword());

  const unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < timeoutMs) {
    delay(250);
  }

  refreshIpCache();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected ip=%s rssi=%d\n", ipAddress, WiFi.RSSI());
    return true;
  }

  Serial.println(F("ERR WiFi connect timeout"));
  return false;
}

bool wifiManagerSyncClock() {
  if (!wifiManagerIsConnected()) {
    return false;
  }

  // Deliberately re-syncs on every call, same as modemManagerSyncClock()
  // (2026-07-23 lesson): between real syncs the system clock free-runs on
  // the ESP32's uncalibrated internal oscillator through each multi-hour
  // deep sleep, and rtcClockSyncFromSystemTimeIfNeeded() trusts it as the
  // DS3231's source of truth — "already plausible" is not "still correct".
  configTime(0, 0, "pool.ntp.org");
  Serial.println(F("Clock: SNTP sync via WiFi..."));

  constexpr time_t kPlausibleEpoch = 1704067200;  // 2024-01-01T00:00:00Z
  const unsigned long deadline = millis() + WIFI_SNTP_TIMEOUT_MS;
  while (static_cast<long>(millis() - deadline) < 0) {
    const time_t now = time(nullptr);
    if (now >= kPlausibleEpoch) {
      struct tm utc;
      gmtime_r(&now, &utc);
      Serial.printf("Clock synced: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                    utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday, utc.tm_hour,
                    utc.tm_min, utc.tm_sec);
      return true;
    }
    delay(100);
  }

  Serial.println(F("ERR SNTP sync timeout"));
  return false;
}

void wifiManagerDisconnect() {
  if (!started) {
    return;
  }
  WiFi.disconnect(true);
  refreshIpCache();
}

void wifiManagerLoop() {
  if (!started || connectivityMode() != ConnectivityMode::WifiSta) {
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    refreshIpCache();
    return;
  }

  const unsigned long now = millis();
  if (now - lastReconnectAttemptMs < RECONNECT_INTERVAL_MS) {
    return;
  }
  lastReconnectAttemptMs = now;
  wifiManagerConnect();
}

bool wifiManagerIsConnected() { return started && WiFi.status() == WL_CONNECTED; }

WifiLinkInfo wifiManagerStatus() {
  WifiLinkInfo status{};
  if (!started) {
    snprintf(status.hostname, sizeof(status.hostname), "beekpr-%s", DEVICE_ID);
  } else {
    strncpy(status.hostname, hostname, sizeof(status.hostname) - 1);
  }

  status.connected = wifiManagerIsConnected();
  status.rssi = status.connected ? WiFi.RSSI() : RSSI_DISCONNECTED;
  if (status.connected) {
    refreshIpCache();
    strncpy(status.ip, ipAddress, sizeof(status.ip) - 1);
  }
  return status;
}

void wifiManagerShow() {
  const WifiLinkInfo status = wifiManagerStatus();
  Serial.printf("wifi_hostname=%s\n", status.hostname);
  Serial.printf("wifi_connected=%s\n", status.connected ? "yes" : "no");
  Serial.printf("wifi_ip=%s\n", status.ip[0] != '\0' ? status.ip : "-");
  Serial.printf("wifi_rssi=%d\n", status.rssi);
  mqttSettingsShow();
}
