#include "mqtt_client.h"

#include <PubSubClient.h>
#include <SSLClient.h>
#include <WiFi.h>

#include "battery_sensor.h"
#include "ca_pem_embed.h"
#include "calibration.h"
#include "config.h"
#include "connectivity_mode.h"
#include "device_settings.h"
#include "gsm_settings.h"
#include "modem_manager.h"
#include "mqtt_settings.h"
#include "telemetry_payload.h"
#include "weight_sensor.h"
#include "wifi_manager.h"

namespace {

constexpr size_t kMqttBufferSize = 512;

// Pump modem.maintain() on every socket I/O — required for TLS over SIM800 GPRS.
class PumpingGsmClient : public Client {
 public:
  int connect(IPAddress ip, uint16_t port) override {
    return connect(ip.toString().c_str(), port);
  }

  int connect(const char *host, uint16_t port) override {
    const int timeoutSec =
        static_cast<int>((MODEM_TLS_HANDSHAKE_TIMEOUT_MS + 999UL) / 1000UL);
    return connect(host, port, timeoutSec);
  }

  int connect(const char *host, uint16_t port, int timeoutSec) {
    modemManagerPump();
    return modemManagerGsmClient().connect(host, port, timeoutSec);
  }

  int connect(IPAddress ip, uint16_t port, int timeoutSec) {
    return connect(ip.toString().c_str(), port, timeoutSec);
  }

  size_t write(uint8_t b) override { return write(&b, 1); }

  size_t write(const uint8_t *buf, size_t size) override {
    modemManagerPump();
    const size_t n = modemManagerGsmClient().write(buf, size);
    modemManagerGsmClient().flush();
    modemManagerPump();
    return n;
  }

  int available() override {
    modemManagerPump();
    return modemManagerGsmClient().available();
  }

  int read() override {
    modemManagerPump();
    return modemManagerGsmClient().read();
  }

  int read(uint8_t *buf, size_t size) override {
    modemManagerPump();
    return modemManagerGsmClient().read(buf, size);
  }

  int peek() override {
    modemManagerPump();
    return modemManagerGsmClient().peek();
  }

  void flush() override { modemManagerGsmClient().flush(); }

  void stop() override { modemManagerGsmClient().stop(); }

  uint8_t connected() override { return modemManagerGsmClient().connected(); }

  operator bool() override { return static_cast<bool>(modemManagerGsmClient()); }
};

WiFiClient &wifiPlainClient() {
  static WiFiClient client;
  return client;
}

PumpingGsmClient &gsmPlainClient() {
  static PumpingGsmClient client;
  return client;
}

SSLClient &wifiTlsClient() {
  static SSLClient client(&wifiPlainClient());
  return client;
}

SSLClient &gsmTlsClient() {
  static SSLClient client(&gsmPlainClient());
  return client;
}

PubSubClient &mqttPubClient() {
  static PubSubClient client(wifiPlainClient());
  return client;
}

const char *mqttStateName(int state) {
  switch (state) {
    case -4:
      return "connection_timeout";
    case -3:
      return "connection_lost";
    case -2:
      return "connect_failed";
    case -1:
      return "disconnected";
    case 0:
      return "connected";
    case 1:
      return "bad_protocol";
    case 2:
      return "bad_client_id";
    case 3:
      return "unavailable";
    case 4:
      return "bad_credentials";
    case 5:
      return "unauthorized";
    default:
      return "unknown";
  }
}

bool usingWifiTransport() {
  return connectivityMode() == ConnectivityMode::WifiSta;
}

Client &plainTransportClient() {
  return usingWifiTransport() ? static_cast<Client &>(wifiPlainClient())
                              : static_cast<Client &>(gsmPlainClient());
}

SSLClient &tlsTransportClient() {
  return usingWifiTransport() ? wifiTlsClient() : gsmTlsClient();
}

bool configureTlsCa(SSLClient &tlsClient) {
#if !CA_PEM_AVAILABLE
  Serial.println(F("ERR certs/ca.pem missing — copy your CA to certs/ca.pem and rebuild"));
  return false;
#else
  static char caPemRam[2400];
  static bool caLoaded = false;
  if (!caLoaded) {
    strncpy_P(caPemRam, CA_PEM, sizeof(caPemRam) - 1);
    caPemRam[sizeof(caPemRam) - 1] = '\0';
    caLoaded = true;
  }
  tlsClient.setCACert(caPemRam);
  return true;
#endif
}

void stopTransport() {
  if (mqttSettingsUseTls()) {
    tlsTransportClient().stop();
  }
  plainTransportClient().stop();
}

bool ensureNetwork(unsigned long networkTimeoutMs) {
  if (usingWifiTransport()) {
    if (!wifiManagerConnect(WIFI_CONNECT_TIMEOUT_MS)) {
      wifiManagerShow();
      return false;
    }
    return true;
  }

  if (!modemManagerEnsureGprs(networkTimeoutMs)) {
    modemManagerShow();
    return false;
  }
  return true;
}

void teardownNetwork() {
  if (usingWifiTransport()) {
    // Keep WiFi up: the config portal and periodic publishes reuse it.
    return;
  }
  modemManagerGprsDisconnect();
}

// The SIM800 buffers TX data; dropping the socket/GPRS right after publish
// discards records the radio has not transmitted yet (slow on 2G). Pump the
// modem for a while so the payload actually leaves the device.
void drainModemTxBeforeClose() {
  if (usingWifiTransport()) {
    return;
  }
  for (uint8_t i = 0; i < 150; i++) {
    modemManagerPump();
    delay(20);
  }
}

String buildCurrentTelemetryJson() {
  const WeightSensorReading reading = weightSensorReadRaw(HX711_RAW_SAMPLES);
  float weightKg = 0.0f;
  float stableKg = 0.0f;
  if (reading.ok && calibrationIsReady()) {
    weightKg = calibrationWeightKg(reading.raw);
    stableKg = weightKg;
  }

  const float batteryV = batterySensorVoltage();
  const int batteryPct = batterySensorPercent();
  const ModemStatus &modem = modemManagerStatus();
  const CellTowerInfo cell = modem.cell.mcc > 0 ? modem.cell : gsmSettingsCellTower();
  const WifiLinkInfo wifi = wifiManagerStatus();

  return buildTelemetryJson(DEVICE_ID, weightKg, stableKg, batteryV, batteryPct,
                            modem.rssi, cell, wifi, settingsTxIntervalSec());
}

bool mqttConnect(unsigned long timeoutMs) {
  PubSubClient &mqttClient = mqttPubClient();
  const char *brokerHost = mqttSettingsHost();
  const uint16_t brokerPort = mqttSettingsPort();
  const bool useTls = mqttSettingsUseTls();

  mqttClient.setServer(brokerHost, brokerPort);
  mqttClient.setBufferSize(kMqttBufferSize);
  mqttClient.setSocketTimeout(static_cast<uint16_t>((timeoutMs + 999UL) / 1000UL));

  stopTransport();

  if (useTls) {
    if (!configureTlsCa(tlsTransportClient())) {
      return false;
    }
    if (!usingWifiTransport()) {
      for (uint8_t i = 0; i < 10; i++) {
        modemManagerPump();
        delay(20);
      }
    }
    tlsTransportClient().setTimeout(timeoutMs);
    mqttClient.setClient(tlsTransportClient());
  } else {
    mqttClient.setClient(plainTransportClient());
  }

  Serial.printf("MQTT connect %s:%u tls=%d user=%s (up to %lus)...\n", brokerHost,
                static_cast<unsigned>(brokerPort), useTls ? 1 : 0, MQTT_USERNAME,
                timeoutMs / 1000UL);

  const unsigned long deadline = millis() + timeoutMs;
  while (!mqttClient.connected()) {
    if (mqttClient.connect(DEVICE_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.println(F("OK MQTT connected"));
      return true;
    }

    Serial.printf("ERR MQTT connect failed state=%d (%s)\n", mqttClient.state(),
                  mqttStateName(mqttClient.state()));
    if (mqttClient.state() == -2 && useTls && !usingWifiTransport()) {
      Serial.println(F("HINT: TLS slow on 2G — retry mqtt; handshake may take 60-120s"));
    }
    if (static_cast<long>(millis() - deadline) >= 0) {
      return false;
    }
    delay(1000);
  }

  return false;
}

bool mqttPublishPayload(const char *topic, const String &payload, bool retained) {
  PubSubClient &mqttClient = mqttPubClient();
  Serial.printf("MQTT publish %s len=%u\n", topic,
                static_cast<unsigned>(payload.length()));
  const bool ok = mqttClient.publish(topic, payload.c_str(), retained);
  if (!ok) {
    Serial.printf("ERR MQTT publish failed state=%d (%s)\n", mqttClient.state(),
                  mqttStateName(mqttClient.state()));
    return false;
  }
  Serial.println(F("OK MQTT published"));
  return true;
}

void mqttDisconnect() {
  PubSubClient &mqttClient = mqttPubClient();
  if (mqttClient.connected()) {
    mqttClient.disconnect();
    Serial.println(F("MQTT disconnected"));
  }
  stopTransport();
}

}  // namespace

bool mqttClientRunTlsSocketTest(unsigned long networkTimeoutMs, unsigned long tlsTimeoutMs) {
  if (!mqttSettingsUseTls()) {
    Serial.println(F("ERR TLS disabled in MQTT settings"));
    mqttSettingsShow();
    return false;
  }

  if (!ensureNetwork(networkTimeoutMs)) {
    return false;
  }

  if (!configureTlsCa(tlsTransportClient())) {
    teardownNetwork();
    mqttSettingsShow();
    return false;
  }

  stopTransport();

  if (!usingWifiTransport()) {
    for (uint8_t i = 0; i < 10; i++) {
      modemManagerPump();
      delay(20);
    }
  }

  SSLClient &tlsClient = tlsTransportClient();
  tlsClient.setTimeout(tlsTimeoutMs);
  const char *brokerHost = mqttSettingsHost();
  const uint16_t brokerPort = mqttSettingsPort();

  Serial.printf("TLS connect %s:%u (up to %lus)...\n", brokerHost,
                static_cast<unsigned>(brokerPort), tlsTimeoutMs / 1000UL);
  if (!usingWifiTransport()) {
    Serial.println(F("Handshake in progress (2G is slow, please wait)..."));
  }

  const bool ok =
      tlsClient.connect(brokerHost, brokerPort, static_cast<int32_t>(tlsTimeoutMs)) == 1;
  if (!ok) {
    Serial.println(F("ERR TLS connect failed"));
    if (!usingWifiTransport()) {
      Serial.println(
          F("HINT: Mosquitto drops TCP at 30s if MQTT CONNECT not sent (keepalive=20 default)"));
    }
    char errBuf[128];
    errBuf[0] = '\0';
    tlsClient.lastError(errBuf, sizeof(errBuf));
    if (errBuf[0] != '\0') {
      Serial.printf("TLS error: %s\n", errBuf);
    }
  } else {
    Serial.println(F("OK TLS connected"));
    tlsClient.stop();
    Serial.println(F("TLS closed"));
  }

  teardownNetwork();
  mqttSettingsShow();
  return ok;
}

bool mqttClientRunGsmTlsSocketTest(unsigned long networkTimeoutMs, unsigned long tlsTimeoutMs) {
  return mqttClientRunTlsSocketTest(networkTimeoutMs, tlsTimeoutMs);
}

bool mqttClientRunPublishTest(unsigned long networkTimeoutMs,
                              unsigned long mqttConnectTimeoutMs) {
  if (!ensureNetwork(networkTimeoutMs)) {
    return false;
  }

  if (mqttSettingsUseTls() && !configureTlsCa(tlsTransportClient())) {
    teardownNetwork();
    mqttSettingsShow();
    return false;
  }

  if (!mqttConnect(mqttConnectTimeoutMs)) {
    mqttDisconnect();
    teardownNetwork();
    mqttSettingsShow();
    return false;
  }

  const String payload = buildCurrentTelemetryJson();
  const bool stateOk = mqttPublishPayload(MQTT_TOPIC_STATE, payload, false);
  const bool availOk =
      mqttPublishPayload(MQTT_TOPIC_AVAILABILITY, F("online"), true);

  drainModemTxBeforeClose();
  mqttDisconnect();
  teardownNetwork();
  mqttSettingsShow();

  return stateOk && availOk;
}

bool mqttClientRunGsmPublishTest(unsigned long networkTimeoutMs,
                                 unsigned long mqttConnectTimeoutMs) {
  return mqttClientRunPublishTest(networkTimeoutMs, mqttConnectTimeoutMs);
}
