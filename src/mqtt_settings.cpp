#include "mqtt_settings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "config.h"

namespace {

constexpr const char *NVS_NAMESPACE = "beekpr_mqtt";
constexpr const char *KEY_HOST = "host";
constexpr const char *KEY_PORT = "port";
constexpr const char *KEY_TLS = "tls";
constexpr size_t HOST_MAX = 64;

Preferences prefs;
char host[HOST_MAX + 1] = "";
uint16_t port = MQTT_BROKER_PORT;
bool useTls = MQTT_USE_TLS != 0;

bool isValidPort(uint16_t value) { return value > 0 && value <= 65535; }

void loadDefaults() {
  strncpy(host, MQTT_BROKER_HOST, HOST_MAX);
  host[HOST_MAX] = '\0';
  port = MQTT_BROKER_PORT;
  useTls = MQTT_USE_TLS != 0;
}

}  // namespace

void mqttSettingsBegin() {
  prefs.begin(NVS_NAMESPACE, false);
  loadDefaults();

  const size_t hostLen = prefs.isKey(KEY_HOST) ? prefs.getBytesLength(KEY_HOST) : 0;
  if (hostLen > 0 && hostLen <= HOST_MAX) {
    prefs.getBytes(KEY_HOST, host, hostLen + 1);
  }

  const uint16_t storedPort = prefs.getUShort(KEY_PORT, 0);
  if (isValidPort(storedPort)) {
    port = storedPort;
  }

  if (prefs.isKey(KEY_TLS)) {
    useTls = prefs.getBool(KEY_TLS, useTls);
  }
}

const char *mqttSettingsHost() { return host; }

uint16_t mqttSettingsPort() { return port; }

bool mqttSettingsUseTls() { return useTls; }

bool mqttSettingsSetBroker(const char *hostValue, uint16_t portValue, bool tls) {
  if (hostValue == nullptr) {
    return false;
  }
  const size_t len = strlen(hostValue);
  if (len == 0 || len > HOST_MAX || !isValidPort(portValue)) {
    return false;
  }

  strncpy(host, hostValue, HOST_MAX);
  host[HOST_MAX] = '\0';
  port = portValue;
  useTls = tls;

  prefs.putBytes(KEY_HOST, host, len + 1);
  prefs.putUShort(KEY_PORT, port);
  prefs.putBool(KEY_TLS, useTls);
  return true;
}

void mqttSettingsShow() {
  Serial.printf("mqtt_broker=%s:%u tls=%s\n", host, static_cast<unsigned>(port),
                useTls ? "yes" : "no");
}
