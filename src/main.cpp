#include <Arduino.h>
#include <math.h>
#include <stdio.h>

#include "app_scheduler.h"
#include "calibration.h"
#include "battery_sensor.h"
#include "config.h"
#include "connectivity_mode.h"
#include "device_settings.h"
#include "gsm_settings.h"
#include "ip5306.h"
#include "maintenance_portal.h"
#include "modem_manager.h"
#include "mqtt_client.h"
#include "mqtt_settings.h"
#include "pins.h"
#include "radio_manager.h"
#include "setup_button.h"
#include "telemetry_payload.h"
#include "temp_sensor.h"
#include "weight_sensor.h"
#include "wifi_manager.h"

namespace {

String commandBuffer;
float weightHistory[SCALE_DISPLAY_MEDIAN_COUNT];
uint8_t weightHistoryCount = 0;
// Bench window: only after setup-button wake (or WiFi power-on for home setup).
// Serial commands extend it; stray UART noise does not.
unsigned long benchDeadlineMs = 0;
bool benchModeActive = false;

void extendBenchWindow() {
  if (!benchModeActive) {
    return;
  }
  benchDeadlineMs = millis() + BENCH_STAY_AWAKE_MS;
}

void startBenchMode() {
  benchModeActive = true;
  extendBenchWindow();
  Serial.printf("Bench mode: publish + deep sleep in %lus (serial commands extend)\n",
                BENCH_STAY_AWAKE_MS / 1000UL);
}

// Headless wake: measure, publish (with retries), deep sleep. Does not return.
void runScheduledCycleAndSleep() {
  if (connectivityMode() != ConnectivityMode::WifiSta) {
    radioPowerDown();
  }
  setupButtonClearBreak();
  appSchedulerRunPublishCycle();
  if (setupButtonBreakWasRequested()) {
    modemManagerGprsDisconnect();
    modemManagerPowerOff();
    return;
  }
  appSchedulerEnterDeepSleep();
}

void printBanner() {
  Serial.println();
  Serial.println(F("=== Smart Hive Scale ==="));
  Serial.printf("Device ID: %s\n", DEVICE_ID);
  Serial.printf("NAU7802: I2C 0x2A, gain 128, 10 SPS (SDA=%d SCL=%d)\n",
                PIN_SCALE_I2C_SDA, PIN_SCALE_I2C_SCL);
  Serial.printf("DS18B20: OneWire GPIO %d (4.7k pull-up to 3.3V)\n",
                PIN_TEMP_ONEWIRE);
  Serial.printf("Setup button: GPIO %d (hold 10s for config portal)\n", PIN_SETUP_BUTTON);
  Serial.println(
      F("Commands: tare | cal <kg> | show | reset | setint <min> | setcell <mcc> <mnc> <lac> <cid>"));
  Serial.println(
      F("           setmode gsm|wifi | setwificred <ssid> <pass> | wificonn | modem | gprs | mqttls | mqtt | send | sleep | modemoff | portal | reboot"));
  Serial.println();
  connectivityShow();
  gsmSettingsShow();
  calibrationShow();
  Serial.println();
}

void pushWeightSample(float weightKg) {
  if (weightHistoryCount < SCALE_DISPLAY_MEDIAN_COUNT) {
    weightHistory[weightHistoryCount++] = weightKg;
    return;
  }

  for (uint8_t i = 1; i < SCALE_DISPLAY_MEDIAN_COUNT; i++) {
    weightHistory[i - 1] = weightHistory[i];
  }
  weightHistory[SCALE_DISPLAY_MEDIAN_COUNT - 1] = weightKg;
}

void printReading(const WeightSensorReading &reading) {
  if (!reading.ok) {
    Serial.println(F("raw=not_ready"));
    return;
  }

  if (!calibrationIsReady()) {
    Serial.printf("raw=%ld weight_kg=uncalibrated\n", reading.raw);
    return;
  }

  const float weightKg = calibrationWeightKg(reading.raw);
  pushWeightSample(weightKg);
  const float stableKg =
      calibrationWeightKgMedian(weightHistory, weightHistoryCount);
  const float tempScaleC = tempSensorReadC();
  const float batteryV = batterySensorVoltage();
  const int batteryPct = batterySensorPercent();

  Serial.printf("raw=%ld weight_kg=%.3f stable_kg=%.3f\n", reading.raw, weightKg,
                stableKg);
  if (isnan(tempScaleC)) {
    Serial.println(F("temp_scale_c=unavailable"));
  } else {
    Serial.printf("temp_scale_c=%.2f\n", tempScaleC);
  }
  Serial.printf("battery_v=%.3f battery_pct=%d\n", batteryV, batteryPct);
  const CellTowerInfo cell = gsmSettingsCellTower();
  const WifiLinkInfo wifi = wifiManagerStatus();
  const int gsmRssi =
      connectivityMode() == ConnectivityMode::Gsm ? modemManagerRssi() : -1;
  if (connectivityMode() == ConnectivityMode::WifiSta) {
    Serial.printf("wifi_connected=%s wifi_ip=%s wifi_rssi=%d\n",
                  wifi.connected ? "yes" : "no",
                  wifi.ip[0] != '\0' ? wifi.ip : "-", wifi.rssi);
  }
  const String payload =
      buildTelemetryJson(DEVICE_ID, weightKg, stableKg, tempScaleC, batteryV,
                         batteryPct, gsmRssi, cell, wifi, settingsTxIntervalSec());
  Serial.printf("mqtt_payload=%s\n", payload.c_str());
}

void handleCommand(const String &line) {
  if (line.length() == 0) {
    return;
  }
  extendBenchWindow();

  if (line == "tare") {
    if (calibrationTare()) {
      Serial.printf("OK tare offset=%ld\n", calibrationOffset());
      weightHistoryCount = 0;
    } else {
      Serial.println(F("ERR tare failed"));
    }
    return;
  }

  if (line == "show") {
    connectivityShow();
    if (connectivityMode() == ConnectivityMode::WifiSta) {
      wifiManagerShow();
    } else {
      modemManagerShow();
    }
    gsmSettingsShow();
    settingsShow();
    calibrationShow();
    return;
  }

  if (line == "reboot") {
    Serial.println(F("OK rebooting"));
    delay(200);
    ESP.restart();
    return;
  }

  if (line == "portal" || line == "wifiap") {
    maintenancePortalBegin(true);
    return;
  }

  if (line == "modem") {
    modemManagerRunTest(MODEM_NETWORK_TIMEOUT_MS);
    return;
  }

  if (line == "gprs") {
    modemManagerRunGprsTest(MODEM_NETWORK_TIMEOUT_MS, MODEM_TCP_CONNECT_TIMEOUT_MS);
    return;
  }

  if (line == "mqttls") {
    mqttClientRunTlsSocketTest(MODEM_NETWORK_TIMEOUT_MS, MODEM_TLS_HANDSHAKE_TIMEOUT_MS);
    return;
  }

  if (line == "mqtt") {
    mqttClientRunPublishTest(MODEM_NETWORK_TIMEOUT_MS, MODEM_MQTT_CONNECT_TIMEOUT_MS);
    return;
  }

  if (line == "send") {
    appSchedulerRunPublishCycle();
    return;
  }

  if (line == "sleep") {
    Serial.println(F("OK entering deep sleep"));
    appSchedulerEnterDeepSleep();
    return;
  }

  if (line == "modemoff") {
    modemManagerPowerOff();
    return;
  }

  if (line == "wificonn") {
    if (connectivityMode() != ConnectivityMode::WifiSta) {
      Serial.println(F("ERR setmode wifi first"));
      return;
    }
    wifiManagerBegin();
    if (wifiManagerConnect(WIFI_CONNECT_TIMEOUT_MS)) {
      wifiManagerShow();
    }
    return;
  }

  if (line.startsWith("setmode ")) {
    ConnectivityMode newMode;
    const String modeName = line.substring(8);
    if (!connectivityParseMode(modeName.c_str(), &newMode)) {
      Serial.println(F("ERR usage: setmode gsm|wifi"));
      return;
    }
    if (newMode == ConnectivityMode::WifiSta && !connectivityHasWifiCredentials()) {
      Serial.println(F("ERR set WiFi credentials first: setwificred <ssid> <pass>"));
      return;
    }
    connectivitySetMode(newMode);
    Serial.printf("OK connectivity_mode=%s (reboot to apply)\n",
                  connectivityModeName(newMode));
    return;
  }

  if (line.startsWith("setwificred ")) {
    const int spaceIdx = line.indexOf(' ', 12);
    if (spaceIdx < 0) {
      Serial.println(F("ERR usage: setwificred <ssid> <pass>"));
      return;
    }
    const String ssid = line.substring(12, spaceIdx);
    const String password = line.substring(spaceIdx + 1);
    if (ssid.length() == 0) {
      Serial.println(F("ERR usage: setwificred <ssid> <pass>"));
      return;
    }
    if (!connectivitySetWifiCredentials(ssid.c_str(), password.c_str())) {
      Serial.println(F("ERR setwificred failed (ssid/pass length?)"));
      return;
    }
    Serial.printf("OK wifi_ssid=%s\n", connectivityWifiSsid());
    return;
  }

  if (line == "reset") {
    calibrationReset();
    weightHistoryCount = 0;
    return;
  }

  if (line.startsWith("setint ")) {
    const int minutes = line.substring(7).toInt();
    if (minutes <= 0) {
      Serial.println(F("ERR usage: setint <minutes 1..1440>"));
      return;
    }
    if (!settingsSetTxIntervalMin(static_cast<uint16_t>(minutes))) {
      Serial.println(F("ERR setint out of range (1..1440)"));
      return;
    }
    settingsShow();
    return;
  }

  if (line.startsWith("setcell ")) {
    int mcc = 0;
    int mnc = 0;
    int lac = 0;
    int cid = 0;
    const int parsed =
        sscanf(line.c_str(), "setcell %d %d %d %d", &mcc, &mnc, &lac, &cid);
    if (parsed != 4) {
      Serial.println(F("ERR usage: setcell <mcc> <mnc> <lac> <cid>"));
      return;
    }
    const CellTowerInfo cell{mcc, mnc, lac, cid};
    gsmSettingsSetCellTower(cell);
    Serial.printf("OK cell_mcc=%d cell_mnc=%d cell_lac=%d cell_cid=%d\n", cell.mcc, cell.mnc,
                  cell.lac, cell.cid);
    return;
  }

  if (line.startsWith("cal ")) {
    const float knownKg = line.substring(4).toFloat();
    if (knownKg <= 0.0f) {
      Serial.println(F("ERR usage: cal <kg>"));
      return;
    }

    if (calibrationOffset() == 0 && !calibrationIsReady()) {
      Serial.println(F("ERR run tare first"));
      return;
    }

    if (calibrationCalibrate(knownKg)) {
      Serial.printf("OK cal scale=%.3f\n", calibrationScale());
      weightHistoryCount = 0;
    } else {
      Serial.println(F("ERR cal failed"));
    }
    return;
  }

  Serial.println(
      F("ERR unknown command (tare | cal <kg> | show | reset | setint | setcell | setmode | setwificred | wificonn | modem | gprs | mqttls | mqtt | send | sleep | modemoff | portal | reboot)"));
}

// Escape hatch before a headless GSM publish cycle: the cycle blocks for
// minutes and ignores input, so give the user a short window to reach bench
// mode (press the setup button or send any serial byte).
bool benchEscapeRequested() {
  Serial.println(F("Publish cycle starts in 5s — press setup button or hit Enter for bench mode"));
  const unsigned long deadline = millis() + 5000UL;
  while (static_cast<long>(millis() - deadline) < 0) {
    if (digitalRead(PIN_SETUP_BUTTON) == LOW || Serial.available() > 0) {
      while (Serial.available() > 0) {
        Serial.read();
      }
      Serial.println(F("Bench mode requested — skipping publish cycle"));
      return true;
    }
    delay(10);
  }
  return false;
}

void pollSerialCommands() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\n' || ch == '\r') {
      // Swallow the rest of a CRLF before dispatching: blocking commands
      // (mqtt/send) poll Serial.available() as an abort signal and a stray
      // '\n' would cancel them instantly.
      while (Serial.available() > 0) {
        const int next = Serial.peek();
        if (next != '\n' && next != '\r') {
          break;
        }
        Serial.read();
      }
      handleCommand(commandBuffer);
      commandBuffer = "";
      continue;
    }
    commandBuffer += ch;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);

  settingsBegin();
  connectivityBegin();
  gsmSettingsBegin();
  mqttSettingsBegin();
  weightSensorBegin();
  tempSensorBegin();
  batterySensorBegin();
  calibrationBegin();
  setupButtonBegin();

  ip5306Begin();
  ip5306SetBoostKeepOn(true);

  const WakeCause wakeCause = appSchedulerWakeCause();
  Serial.printf("wake_cause=%s\n", appSchedulerWakeCauseName(wakeCause));

  if (wakeCause == WakeCause::Timer) {
    runScheduledCycleAndSleep();
  }

  if (wakeCause == WakeCause::PowerOn && connectivityMode() == ConnectivityMode::Gsm) {
    // Field GSM: cold boot / battery connect → publish once, then sleep.
    // The 5s escape window lets you reach bench mode (fix bad settings)
    // instead of being locked into a minutes-long blocking publish cycle.
    if (!benchEscapeRequested()) {
      runScheduledCycleAndSleep();
    }
  }

  // Button wake, or WiFi power-on (home setup): interactive bench mode.
  if (connectivityMode() == ConnectivityMode::WifiSta) {
    wifiManagerBegin();
    wifiManagerConnect(WIFI_CONNECT_TIMEOUT_MS);
  } else {
    radioPowerDown();
  }

  printBanner();
  if (connectivityMode() == ConnectivityMode::WifiSta) {
    wifiManagerShow();
    if (wifiManagerIsConnected()) {
      maintenancePortalBegin();
    }
  }
  startBenchMode();
}

void loop() {
  pollSerialCommands();

  if (setupButtonPortalRequested()) {
    maintenancePortalBegin(true);
  }

  if (maintenancePortalIsActive()) {
    maintenancePortalLoop();
    // AP config mode: skip sensors/radio work to keep current draw low (brownout).
    // In WiFi STA mode the portal runs alongside normal operation.
    if (!maintenancePortalIsStaMode()) {
      delay(10);
      return;
    }
  }

  if (connectivityMode() == ConnectivityMode::WifiSta) {
    wifiManagerLoop();
    if (!maintenancePortalIsActive() && wifiManagerIsConnected()) {
      maintenancePortalBegin();
    }
  }

  // Bench window over: publish and deep sleep. AP portal early-returns above.
  if (benchModeActive && static_cast<long>(millis() - benchDeadlineMs) >= 0) {
    Serial.println(F("Bench window expired — publishing and going to sleep"));
    runScheduledCycleAndSleep();
    // Only reached when the cycle was aborted (button/serial): stay in bench
    // mode with a fresh window instead of re-triggering the publish instantly.
    extendBenchWindow();
  }

  if (!benchModeActive) {
    return;
  }

  WeightSensorReading reading = weightSensorReadRaw(SCALE_RAW_SAMPLES);
  printReading(reading);

  delay(SCALE_READ_INTERVAL_MS);
}
