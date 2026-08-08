#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <math.h>
#include <stdio.h>

#include "app_scheduler.h"
#include "calibration.h"
#include "battery_sensor.h"
#include "config.h"
#include "connectivity_mode.h"
#include "device_settings.h"
#include "gsm_settings.h"
#include "i2c_mux.h"
#include "maintenance_portal.h"
#include "modem_manager.h"
#include "mqtt_client.h"
#include "mqtt_settings.h"
#include "pins.h"
#include "radio_manager.h"
#include "rtc_clock.h"
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
  Serial.printf("NAU7802 x%u: I2C 0x2A behind PCA9548A 0x%02X ch", NUM_CORNERS, I2C_MUX_ADDR);
  for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
    Serial.printf("%c%u", corner == 0 ? ' ' : '/', CORNER_MUX_CHANNEL[corner]);
  }
  Serial.printf(" (bus SDA=%d SCL=%d)\n", PIN_I2C_SDA, PIN_I2C_SCL);
  Serial.printf("DS18B20: OneWire GPIO %d (4.7k pull-up to 3.3V)\n",
                PIN_TEMP_ONEWIRE);
  rtcClockShow();
  Serial.printf("Setup button: GPIO %d (hold 10s for config portal)\n", PIN_SETUP_BUTTON);
  Serial.println(
      F("Commands: tare | cal <kg> | show | reset | setint <min> | setcell <mcc> <mnc> <lac> <cid>"));
  Serial.println(
      F("           setmode gsm|wifi | setwificred <ssid> <pass> | wificonn | modem | gprs | mqttls | mqtt | send | sleep | modemoff | battery | i2cscan | portal | reboot"));
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

void printReading(const ScaleReading &reading) {
  String rawLine = "raw";
  for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
    rawLine += " c";
    rawLine += String(corner);
    rawLine += '=';
    rawLine += reading.cornerOk[corner] ? String(reading.cornerRaw[corner])
                                        : String("not_ready");
  }

  if (!reading.ok) {
    Serial.println(rawLine);
    return;
  }

  if (!calibrationIsReady()) {
    Serial.printf("%s weight_kg=uncalibrated\n", rawLine.c_str());
    return;
  }

  pushWeightSample(reading.totalKg);
  const float stableKg =
      calibrationWeightKgMedian(weightHistory, weightHistoryCount);
  const float tempScaleC = tempSensorReadC();
  const float batteryV = batterySensorVoltage();
  const int batteryPct = batterySensorPercent();

  Serial.println(rawLine);
  Serial.print(F("corners_kg"));
  for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
    Serial.printf(" c%u=%.3f", corner, reading.cornerKg[corner]);
  }
  Serial.printf(" weight_kg=%.3f stable_kg=%.3f\n", reading.totalKg, stableKg);
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
  const String payload = buildTelemetryJson(
      DEVICE_ID, reading.totalKg, stableKg, reading.cornerKg, tempScaleC,
      batteryV, batteryPct, gsmRssi, cell, wifi, settingsTxIntervalSec(),
      rtcClockNowIso8601());
  Serial.printf("mqtt_payload=%s\n", payload.c_str());
}

void handleCommand(const String &line) {
  if (line.length() == 0) {
    return;
  }
  extendBenchWindow();

  if (line == "tare") {
    if (calibrationTare()) {
      Serial.print(F("OK tare offsets"));
      for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
        Serial.printf(" c%u=%ld", corner, calibrationOffset(corner));
      }
      Serial.println();
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
    rtcClockShow();
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

  if (line == "battery") {
    // Single-point divider calibration (plan E2): compare against a
    // multimeter on the raw battery node and scale BATTERY_DIVIDER_RATIO.
    const float batteryV = batterySensorVoltage();
    Serial.printf("battery_v=%.3f battery_pct=%d divider_ratio=%.4f\n",
                  batteryV, batterySensorPercent(), BATTERY_DIVIDER_RATIO);
    Serial.println(
        F("calibrate: BATTERY_DIVIDER_RATIO = divider_ratio * V_multimeter / battery_v (config.h)"));
    return;
  }

  if (line == "i2cscan") {
    // Upstream scan first (expect PCA9548A 0x70 + DS3231 0x68), then probe
    // each mux channel for its corner's NAU7802 (0x2A). The step-13
    // bring-up tool (doc/rework-implementation-plan.md E3).
    Serial.printf("I2C scan (SDA=%d SCL=%d):\n", PIN_I2C_SDA, PIN_I2C_SCL);
    i2cMuxDeselectAll();
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        Serial.printf("  found 0x%02X\n", addr);
        found++;
      }
    }
    if (found == 0) {
      Serial.println(F("  no devices found — check wiring/pull-ups"));
    }
    if (!i2cMuxIsPresent()) {
      Serial.printf("  PCA9548A 0x%02X missing — corner channels not scannable\n",
                    I2C_MUX_ADDR);
      return;
    }
    for (uint8_t corner = 0; corner < NUM_CORNERS; corner++) {
      const uint8_t channel = CORNER_MUX_CHANNEL[corner];
      i2cMuxSelect(channel);
      Wire.beginTransmission(0x2A);
      const bool nauFound = Wire.endTransmission() == 0;
      Serial.printf("  corner %u (mux ch%u): NAU7802 0x2A %s\n", corner, channel,
                    nauFound ? "found" : "MISSING");
    }
    i2cMuxDeselectAll();
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

    if (!calibrationHasTare()) {
      Serial.println(F("ERR run tare first"));
      return;
    }

    if (calibrationCalibrate(knownKg)) {
      Serial.printf("OK cal span=%.3f\n", calibrationScale());
      weightHistoryCount = 0;
    } else {
      Serial.println(F("ERR cal failed"));
    }
    return;
  }

  Serial.println(
      F("ERR unknown command (tare | cal <kg> | show | reset | setint | setcell | setmode | setwificred | wificonn | modem | gprs | mqttls | mqtt | send | sleep | modemoff | i2cscan | portal | reboot)"));
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
  // Modem rail off before anything else: CR-SJ5530 #2's EN is enabled by
  // default and GPIO4 floats through boot, so the SIM800L rail is up (and
  // the module auto-booting) from power-on until this runs (spec.md §10).
  // Release the previous sleep's latch first so the write takes effect.
  gpio_hold_dis(static_cast<gpio_num_t>(PIN_MODEM_EN));
  pinMode(PIN_MODEM_EN, OUTPUT);
  digitalWrite(PIN_MODEM_EN, LOW);

  Serial.begin(115200);

  const WakeCause wakeCause = appSchedulerWakeCause();
  // Timer wake only exists in the DS3231-absent fallback, where it IS the
  // scheduled report — handled by the normal dispatch below.

  delay(1000);

  settingsBegin();
  connectivityBegin();
  gsmSettingsBegin();
  mqttSettingsBegin();

  // Single shared I2C bus: PCA9548A mux + DS3231 upstream, NAU7802s behind
  // the mux channels.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  i2cMuxBegin();
  rtcClockBegin();

  weightSensorBegin();
  tempSensorBegin();
  batterySensorBegin();
  calibrationBegin();
  setupButtonBegin();

  Serial.printf("wake_cause=%s\n", appSchedulerWakeCauseName(wakeCause));

  if (wakeCause == WakeCause::RtcAlarm || wakeCause == WakeCause::Timer) {
    // Reaching here with Timer only happens via the DS3231-absent fallback
    // above — RtcAlarm is the normal, precise "report is due" signal.
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

  const ScaleReading reading = calibrationReadAll(SCALE_LIVE_SAMPLES);
  printReading(reading);

  delay(SCALE_READ_INTERVAL_MS);
}
