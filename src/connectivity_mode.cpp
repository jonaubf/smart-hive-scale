#include "connectivity_mode.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "config.h"
#include "mqtt_settings.h"

namespace {

constexpr const char *NVS_NAMESPACE = "beekpr_conn";
constexpr const char *KEY_MODE = "mode";
constexpr const char *KEY_WIFI_SSID = "wifi_ssid";
constexpr const char *KEY_WIFI_PASS = "wifi_pass";
constexpr size_t WIFI_SSID_MAX = 32;
constexpr size_t WIFI_PASS_MAX = 64;

Preferences prefs;
ConnectivityMode mode = ConnectivityMode::Gsm;
char wifiSsid[WIFI_SSID_MAX + 1] = "";
char wifiPass[WIFI_PASS_MAX + 1] = "";

bool modeIsValid(uint8_t raw) {
  return raw <= static_cast<uint8_t>(ConnectivityMode::WifiSta);
}

}  // namespace

void connectivityBegin() {
  prefs.begin(NVS_NAMESPACE, false);
  const uint8_t stored = prefs.getUChar(KEY_MODE, static_cast<uint8_t>(ConnectivityMode::Gsm));
  if (!modeIsValid(stored)) {
    mode = ConnectivityMode::Gsm;
    prefs.putUChar(KEY_MODE, static_cast<uint8_t>(mode));
  } else {
    mode = static_cast<ConnectivityMode>(stored);
  }

  const size_t ssidLen = prefs.getBytesLength(KEY_WIFI_SSID);
  if (ssidLen > 0 && ssidLen <= WIFI_SSID_MAX) {
    prefs.getBytes(KEY_WIFI_SSID, wifiSsid, ssidLen + 1);
  } else {
    wifiSsid[0] = '\0';
  }

  const size_t passLen = prefs.getBytesLength(KEY_WIFI_PASS);
  if (passLen > 0 && passLen <= WIFI_PASS_MAX) {
    prefs.getBytes(KEY_WIFI_PASS, wifiPass, passLen + 1);
  } else {
    wifiPass[0] = '\0';
  }
}

ConnectivityMode connectivityMode() { return mode; }

const char *connectivityModeName(ConnectivityMode value) {
  switch (value) {
    case ConnectivityMode::Gsm:
      return "gsm";
    case ConnectivityMode::WifiSta:
      return "wifi";
  }
  return "unknown";
}

bool connectivitySetMode(ConnectivityMode value) {
  mode = value;
  prefs.putUChar(KEY_MODE, static_cast<uint8_t>(value));
  return true;
}

bool connectivityParseMode(const char *name, ConnectivityMode *outMode) {
  if (name == nullptr || outMode == nullptr) {
    return false;
  }
  if (strcmp(name, "gsm") == 0) {
    *outMode = ConnectivityMode::Gsm;
    return true;
  }
  if (strcmp(name, "wifi") == 0) {
    *outMode = ConnectivityMode::WifiSta;
    return true;
  }
  return false;
}

bool connectivitySetWifiCredentials(const char *ssid, const char *password) {
  if (ssid == nullptr || password == nullptr) {
    return false;
  }
  const size_t ssidLen = strlen(ssid);
  const size_t passLen = strlen(password);
  if (ssidLen == 0 || ssidLen > WIFI_SSID_MAX || passLen > WIFI_PASS_MAX) {
    return false;
  }

  strncpy(wifiSsid, ssid, WIFI_SSID_MAX);
  wifiSsid[WIFI_SSID_MAX] = '\0';
  strncpy(wifiPass, password, WIFI_PASS_MAX);
  wifiPass[WIFI_PASS_MAX] = '\0';

  prefs.putBytes(KEY_WIFI_SSID, wifiSsid, ssidLen + 1);
  prefs.putBytes(KEY_WIFI_PASS, wifiPass, passLen + 1);
  return true;
}

const char *connectivityWifiSsid() { return wifiSsid; }

const char *connectivityWifiPassword() { return wifiPass; }

bool connectivityHasWifiCredentials() { return wifiSsid[0] != '\0'; }

void connectivityShow() {
  Serial.printf("connectivity_mode=%s\n", connectivityModeName(mode));
  Serial.printf("wifi_ssid=%s wifi_configured=%s\n", wifiSsid,
                connectivityHasWifiCredentials() ? "yes" : "no");
  mqttSettingsShow();
}
