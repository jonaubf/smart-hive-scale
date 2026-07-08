#include "modem_manager.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <TinyGsmClient.h>

#include "config.h"
#include "gsm_settings.h"
#include "mqtt_settings.h"
#include "ip5306.h"
#include "pins.h"

namespace {

HardwareSerial SerialAT(2);
TinyGsm modem(SerialAT);
// Exactly one TinyGSM client may exist per mux: each instance registers itself
// in the modem's sockets[mux] table and the last registration wins. A second
// client on mux 0 (the old TinyGsmClientSecure) silently stole all RX data.
TinyGsmClient gsmClient(modem);

ModemStatus status{};
bool hardwareReady = false;
bool caCertificateInstalled = false;

constexpr char kCaCertPath[] = "C:\\USER\\CA.CRT";

const char *simStatusName(SimStatus simStatus) {
  switch (simStatus) {
    case SIM_READY:
      return "ready";
    case SIM_LOCKED:
      return "locked";
    case SIM_ERROR:
      return "error";
    default:
      return "unknown";
  }
}

const char *registrationName(int regStatus) {
  switch (regStatus) {
    case 0:
      return "not_registered";
    case 1:
      return "home";
    case 2:
      return "searching";
    case 3:
      return "denied";
    case 4:
      return "unknown";
    case 5:
      return "roaming";
    default:
      return "invalid";
  }
}

int parseHexField(const char *text) {
  if (text == nullptr || text[0] == '\0') {
    return -1;
  }
  return static_cast<int>(strtol(text, nullptr, 16));
}

void flushSerialAt() {
  while (SerialAT.available() > 0) {
    SerialAT.read();
  }
}

void drainOkResponse(unsigned long timeoutMs) {
  const unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (SerialAT.find("OK")) {
      return;
    }
    delay(10);
  }
}

String readResponseLine(unsigned long timeoutMs) {
  String line;
  const unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (SerialAT.available() > 0) {
      const char ch = static_cast<char>(SerialAT.read());
      if (ch == '\r') {
        continue;
      }
      if (ch == '\n') {
        if (line.length() > 0) {
          return line;
        }
        continue;
      }
      line += ch;
    }
    delay(10);
  }
  return line;
}

bool readOperatorNumeric(int *mcc, int *mnc) {
  flushSerialAt();
  SerialAT.println("AT+COPS=3,2");
  drainOkResponse(2000);

  flushSerialAt();
  SerialAT.println("AT+COPS?");
  const unsigned long start = millis();
  while (millis() - start < 3000) {
    const String line = readResponseLine(200);
    if (!line.startsWith("+COPS:")) {
      continue;
    }

    const int firstQuote = line.indexOf('"');
    const int secondQuote = line.indexOf('"', firstQuote + 1);
    if (firstQuote < 0 || secondQuote <= firstQuote) {
      return false;
    }

    const String numeric = line.substring(firstQuote + 1, secondQuote);
    if (numeric.length() < 5) {
      return false;
    }

    *mcc = numeric.substring(0, 3).toInt();
    *mnc = numeric.substring(3).toInt();
    return true;
  }
  return false;
}

bool readCellLocation(CellTowerInfo *cell) {
  flushSerialAt();
  SerialAT.println("AT+CREG=2");
  drainOkResponse(2000);

  flushSerialAt();
  SerialAT.println("AT+CREG?");
  const unsigned long start = millis();
  while (millis() - start < 3000) {
    const String line = readResponseLine(200);
    if (!line.startsWith("+CREG:")) {
      continue;
    }

    const int firstComma = line.indexOf(',');
    const int secondComma = line.indexOf(',', firstComma + 1);
    if (firstComma < 0 || secondComma < 0) {
      return false;
    }

    const int regStatus = line.substring(firstComma + 1, secondComma).toInt();
    if (regStatus != 1 && regStatus != 5) {
      return false;
    }

    String lacHex = line.substring(secondComma + 1);
    const int thirdComma = lacHex.indexOf(',');
    if (thirdComma < 0) {
      return false;
    }

    String cidHex = lacHex.substring(thirdComma + 1);
    lacHex = lacHex.substring(0, thirdComma);
    lacHex.replace("\"", "");
    cidHex.replace("\"", "");
    cidHex.trim();

    cell->lac = parseHexField(lacHex.c_str());
    cell->cid = parseHexField(cidHex.c_str());
    return cell->lac >= 0 && cell->cid >= 0;
  }
  return false;
}

void pulseModemPowerKey() {
  digitalWrite(PIN_MODEM_PWRKEY, HIGH);
  delay(100);
  digitalWrite(PIN_MODEM_PWRKEY, LOW);
  delay(1000);
  digitalWrite(PIN_MODEM_PWRKEY, HIGH);
}

void configureModemPins() {
  pinMode(PIN_MODEM_RST, OUTPUT);
  pinMode(PIN_MODEM_PWRKEY, OUTPUT);
  pinMode(PIN_MODEM_POWER, OUTPUT);

  digitalWrite(PIN_MODEM_RST, HIGH);
  digitalWrite(PIN_MODEM_PWRKEY, LOW);
  digitalWrite(PIN_MODEM_POWER, LOW);
}

bool ensureNetwork(unsigned long timeoutMs) {
  if (!status.powered && !modemManagerPowerOn()) {
    return false;
  }
  if (!status.initialized && !modemManagerRestart()) {
    return false;
  }
  if (!status.networkRegistered && !modemManagerWaitForNetwork(timeoutMs)) {
    return false;
  }
  return status.networkRegistered;
}

}  // namespace

void modemManagerBeginHardware() {
  if (hardwareReady) {
    return;
  }

  ip5306Begin();
  if (!ip5306SetBoostKeepOn(true)) {
    Serial.println(F("WARN IP5306 boost keep-on failed"));
  }

  configureModemPins();
  hardwareReady = true;
}

bool modemManagerPowerOn() {
  modemManagerBeginHardware();

  digitalWrite(PIN_MODEM_POWER, HIGH);
  pulseModemPowerKey();

  SerialAT.begin(115200, SERIAL_8N1, PIN_MODEM_RX, PIN_MODEM_TX);
  delay(1500);

  status.powered = true;
  status.initialized = false;
  Serial.println(F("Modem power on"));
  return true;
}

bool modemManagerPowerOff() {
  if (!status.powered) {
    return true;
  }

  if (status.initialized) {
    modem.poweroff();
    status.initialized = false;
  }

  SerialAT.end();
  digitalWrite(PIN_MODEM_POWER, LOW);
  status.powered = false;
  status.networkRegistered = false;
  status.gprsConnected = false;
  status.gprsIp[0] = '\0';
  status.rssi = -1;
  Serial.println(F("Modem power off"));
  return true;
}

bool modemManagerRestart() {
  if (!status.powered) {
    if (!modemManagerPowerOn()) {
      return false;
    }
  }

  Serial.println(F("Modem initializing..."));
  const bool ok = (GSM_PIN[0] != '\0') ? modem.restart(GSM_PIN) : modem.restart();
  if (!ok) {
    Serial.println(F("ERR modem restart failed"));
    status.initialized = false;
    return false;
  }

  status.initialized = true;

  String info = modem.getModemInfo();
  info.toCharArray(status.modemInfo, sizeof(status.modemInfo));
  Serial.printf("modem_info=%s\n", status.modemInfo);

  const SimStatus simStatus = modem.getSimStatus();
  Serial.printf("sim_status=%s\n", simStatusName(simStatus));
  if (simStatus == SIM_LOCKED) {
    Serial.println(F("ERR SIM locked — set GSM_PIN in .env if needed"));
    return false;
  }
  if (simStatus != SIM_READY) {
    Serial.println(F("ERR SIM not ready — check card insertion"));
    return false;
  }

  String imei = modem.getIMEI();
  imei.toCharArray(status.imei, sizeof(status.imei));
  Serial.printf("imei=%s\n", status.imei);
  return true;
}

bool modemManagerWaitForNetwork(unsigned long timeoutMs) {
  if (!status.initialized) {
    Serial.println(F("ERR modem not initialized"));
    return false;
  }

  Serial.printf("Waiting for network (%lu s max)...\n", timeoutMs / 1000UL);
  const bool ok = modem.waitForNetwork(timeoutMs, true);
  status.networkRegistered = modem.isNetworkConnected();
  if (!ok) {
    Serial.println(F("ERR network registration timeout"));
    return false;
  }

  Serial.println(F("OK network registered"));
  return true;
}

bool modemManagerGprsConnect() {
  if (!status.initialized) {
    Serial.println(F("ERR modem not initialized"));
    return false;
  }
  if (!status.networkRegistered) {
    Serial.println(F("ERR network not registered"));
    return false;
  }

  if (modem.isGprsConnected()) {
    status.gprsConnected = true;
    IPAddress ip = modem.localIP();
    strncpy(status.gprsIp, ip.toString().c_str(), sizeof(status.gprsIp) - 1);
    status.gprsIp[sizeof(status.gprsIp) - 1] = '\0';
    Serial.printf("GPRS already connected ip=%s\n", status.gprsIp);
    return true;
  }

  Serial.printf("GPRS connecting apn=%s user=%s...\n", gsmSettingsApn(),
                gsmSettingsUser()[0] != '\0' ? gsmSettingsUser() : "(empty)");
  const bool ok =
      modem.gprsConnect(gsmSettingsApn(), gsmSettingsUser(), gsmSettingsPass());
  status.gprsConnected = ok && modem.isGprsConnected();
  if (!status.gprsConnected) {
    Serial.println(F("ERR GPRS connect failed"));
    status.gprsIp[0] = '\0';
    return false;
  }

  IPAddress ip = modem.localIP();
  strncpy(status.gprsIp, ip.toString().c_str(), sizeof(status.gprsIp) - 1);
  status.gprsIp[sizeof(status.gprsIp) - 1] = '\0';
  Serial.printf("OK GPRS connected ip=%s\n", status.gprsIp);
  return true;
}

bool modemManagerGprsDisconnect() {
  gsmClient.stop();
  if (status.initialized && modem.isGprsConnected()) {
    modem.gprsDisconnect();
  }
  status.gprsConnected = false;
  status.gprsIp[0] = '\0';
  Serial.println(F("GPRS disconnected"));
  return true;
}

bool modemManagerTcpTest(const char *host, uint16_t port, unsigned long timeoutMs) {
  if (host == nullptr || host[0] == '\0') {
    Serial.println(F("ERR TCP host not configured"));
    return false;
  }
  if (!status.gprsConnected && !modem.isGprsConnected()) {
    Serial.println(F("ERR GPRS not connected"));
    return false;
  }

  gsmClient.stop();
  const uint16_t timeoutSec =
      static_cast<uint16_t>((timeoutMs + 999UL) / 1000UL);
  Serial.printf("TCP connect %s:%u (timeout %us)...\n", host, port, timeoutSec);

  const bool ok = gsmClient.connect(host, port, timeoutSec);
  if (!ok) {
    Serial.println(F("ERR TCP connect failed"));
    return false;
  }

  Serial.println(F("OK TCP connected"));
  gsmClient.stop();
  Serial.println(F("TCP closed"));
  return true;
}

bool modemManagerEnsureGprs(unsigned long networkTimeoutMs) {
  if (!ensureNetwork(networkTimeoutMs)) {
    return false;
  }
  modemManagerUpdateStatus();
  if (!modemManagerGprsConnect()) {
    return false;
  }
  // TLS certificate validation needs a correct clock; without sync the ESP32
  // thinks it's 1970 and every certificate is "not yet valid".
  modemManagerSyncClock();
  return true;
}

bool modemManagerSyncClock() {
  if (!status.initialized) {
    return false;
  }

  // Skip if the RTC already holds a plausible date (later than 2023-01-01).
  if (time(nullptr) > 1672531200) {
    return true;
  }

  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  float tzHours = 0.0f;
  bool got = modem.getNetworkTime(&year, &month, &day, &hour, &minute, &second,
                                  &tzHours);

  if (!got || year < 2024) {
    // No NITZ time from the carrier; ask the modem to do NTP over GPRS.
    Serial.println(F("Clock: no network time, trying NTP via GPRS..."));
    modem.NTPServerSync("pool.ntp.org", 0);
    got = modem.getNetworkTime(&year, &month, &day, &hour, &minute, &second,
                               &tzHours);
  }

  if (!got || year < 2024) {
    Serial.println(F("ERR clock sync failed (no NITZ, NTP failed)"));
    return false;
  }

  struct tm timeParts = {};
  timeParts.tm_year = year - 1900;
  timeParts.tm_mon = month - 1;
  timeParts.tm_mday = day;
  timeParts.tm_hour = hour;
  timeParts.tm_min = minute;
  timeParts.tm_sec = second;
  // mktime() interprets in the local zone, which is UTC by default on ESP32;
  // the modem reports local time, so subtract its zone offset to get UTC.
  const time_t utc =
      mktime(&timeParts) - static_cast<time_t>(tzHours * 3600.0f);

  struct timeval tv = {.tv_sec = utc, .tv_usec = 0};
  settimeofday(&tv, nullptr);

  Serial.printf("Clock synced: %04d-%02d-%02d %02d:%02d:%02d (tz %+.1f)\n",
                year, month, day, hour, minute, second,
                static_cast<double>(tzHours));
  return true;
}

bool modemManagerInstallCaCertificate(const uint8_t *data, size_t len) {
  if (data == nullptr || len == 0) {
    Serial.println(F("ERR CA certificate data missing"));
    return false;
  }
  if (!status.initialized) {
    Serial.println(F("ERR modem not initialized"));
    return false;
  }
  if (caCertificateInstalled) {
    return true;
  }

  Serial.println(F("Uploading CA certificate to modem..."));

  modem.sendAT(GF("+FSDEL=\""), kCaCertPath, '"');
  modem.waitResponse(2000L);

  modem.sendAT(GF("+FSCREATE=\""), kCaCertPath, '"');
  if (modem.waitResponse(5000L) != 1) {
    Serial.println(F("ERR modem FSCREATE failed"));
    return false;
  }

  modem.sendAT(GF("+FSWRITE=\""), kCaCertPath, GF("\",0,"), len, GF(",10"));
  if (modem.waitResponse(GF(">")) != 1) {
    Serial.println(F("ERR modem FSWRITE prompt failed"));
    return false;
  }

  modem.stream.write(data, len);
  modem.stream.write('\n');
  modem.stream.flush();
  if (modem.waitResponse(5000L) != 1) {
    Serial.println(F("ERR modem FSWRITE failed"));
    return false;
  }

  modem.sendAT(GF("+SSLSETCERT=\""), kCaCertPath, '"');
  if (modem.waitResponse(5000L) != 1) {
    Serial.println(F("ERR modem SSLSETCERT failed"));
    return false;
  }
  if (modem.waitResponse(10000L, GF(AT_NL "+SSLSETCERT:")) != 1) {
    Serial.println(F("ERR modem SSLSETCERT response timeout"));
    return false;
  }

  const int retCode = modem.stream.readStringUntil('\n').toInt();
  if (retCode != 0) {
    Serial.printf("ERR modem SSLSETCERT code=%d\n", retCode);
    return false;
  }

  caCertificateInstalled = true;
  Serial.println(F("OK CA certificate installed on modem"));
  return true;
}

TinyGsm &modemManagerModem() { return modem; }

TinyGsmClient &modemManagerGsmClient() { return gsmClient; }

bool modemManagerRunGprsTest(unsigned long networkTimeoutMs, unsigned long tcpTimeoutMs) {
  if (!ensureNetwork(networkTimeoutMs)) {
    modemManagerUpdateStatus();
    modemManagerShow();
    return false;
  }
  modemManagerUpdateStatus();

  if (!modemManagerGprsConnect()) {
    modemManagerShow();
    return false;
  }

  const bool tcpOk =
      modemManagerTcpTest(mqttSettingsHost(), mqttSettingsPort(), tcpTimeoutMs);

  modemManagerGprsDisconnect();
  modemManagerShow();
  return tcpOk;
}

bool modemManagerUpdateStatus() {
  if (!status.initialized) {
    return false;
  }

  status.simReady = modem.getSimStatus() == SIM_READY;
  status.networkRegistered = modem.isNetworkConnected();
  status.gprsConnected = modem.isGprsConnected();
  status.rssi = modem.getSignalQuality();

  if (status.gprsConnected) {
    IPAddress ip = modem.localIP();
    strncpy(status.gprsIp, ip.toString().c_str(), sizeof(status.gprsIp) - 1);
    status.gprsIp[sizeof(status.gprsIp) - 1] = '\0';
  } else {
    status.gprsIp[0] = '\0';
  }

  String operatorName = modem.getOperator();
  operatorName.toCharArray(status.operatorName, sizeof(status.operatorName));

  CellTowerInfo cell = status.cell;
  int mcc = cell.mcc;
  int mnc = cell.mnc;
  if (readOperatorNumeric(&mcc, &mnc)) {
    cell.mcc = mcc;
    cell.mnc = mnc;
  }
  if (readCellLocation(&cell)) {
    status.cell = cell;
    gsmSettingsSetCellTower(cell);
  }

  return true;
}

bool modemManagerRunTest(unsigned long networkTimeoutMs) {
  if (!modemManagerPowerOn()) {
    return false;
  }
  if (!modemManagerRestart()) {
    return false;
  }
  if (!modemManagerWaitForNetwork(networkTimeoutMs)) {
    modemManagerUpdateStatus();
    modemManagerShow();
    return false;
  }
  modemManagerUpdateStatus();
  modemManagerShow();
  return status.networkRegistered;
}

const ModemStatus &modemManagerStatus() { return status; }

int modemManagerRssi() {
  if (!status.initialized || !status.networkRegistered) {
    return -1;
  }
  return status.rssi;
}

bool modemManagerIsNetworkRegistered() {
  return status.initialized && status.networkRegistered;
}

bool modemManagerIsGprsConnected() {
  return status.initialized && status.gprsConnected;
}

void modemManagerPump() {
  if (status.initialized) {
    modem.maintain();
  }
}

extern "C" void beekprModemPumpForTls(void) {
  if (status.initialized) {
    modem.maintain();
    modem.maintain();
  }
}

// Queries the SIM800 mid-TLS to see whether sent bytes were ACKed by the
// remote TCP stack and whether unread data is sitting in the modem buffer.
extern "C" void beekprTlsDiagnostic(void) {
  if (!status.initialized) {
    return;
  }

  String resp;
  modem.sendAT(GF("+CIPACK=0"));
  modem.waitResponse(2000L, resp);
  resp.trim();
  resp.replace("\r\n", " | ");
  Serial.printf("[TLS DIAG] CIPACK (tx,acked,unacked): %s\n", resp.c_str());

  resp = "";
  modem.sendAT(GF("+CIPSTATUS=0"));
  modem.waitResponse(2000L, resp);
  resp.trim();
  resp.replace("\r\n", " | ");
  Serial.printf("[TLS DIAG] CIPSTATUS: %s\n", resp.c_str());

  resp = "";
  modem.sendAT(GF("+CIPRXGET=4,0"));
  modem.waitResponse(2000L, resp);
  resp.trim();
  resp.replace("\r\n", " | ");
  Serial.printf("[TLS DIAG] CIPRXGET4 (bytes in modem buf): %s\n", resp.c_str());
}

void modemManagerShow() {
  const int regStatus =
      status.initialized ? static_cast<int>(modem.getRegistrationStatus()) : -1;

  Serial.printf("modem_powered=%s\n", status.powered ? "yes" : "no");
  Serial.printf("modem_initialized=%s\n", status.initialized ? "yes" : "no");
  Serial.printf("sim_ready=%s\n", status.simReady ? "yes" : "no");
  Serial.printf("network_registered=%s\n", status.networkRegistered ? "yes" : "no");
  Serial.printf("gprs_connected=%s\n", status.gprsConnected ? "yes" : "no");
  Serial.printf("gprs_ip=%s\n", status.gprsIp[0] != '\0' ? status.gprsIp : "-");
  if (status.initialized) {
    Serial.printf("registration=%s\n", registrationName(regStatus));
  }
  Serial.printf("rssi=%d\n", status.rssi);
  Serial.printf("operator=%s\n", status.operatorName[0] != '\0' ? status.operatorName : "-");
  Serial.printf("imei=%s\n", status.imei[0] != '\0' ? status.imei : "-");
  Serial.printf("modem_info=%s\n", status.modemInfo[0] != '\0' ? status.modemInfo : "-");
  Serial.printf("gsm_apn=%s\n", gsmSettingsApn());
  mqttSettingsShow();
  Serial.printf("cell_mcc=%d cell_mnc=%d cell_lac=%d cell_cid=%d\n", status.cell.mcc,
                status.cell.mnc, status.cell.lac, status.cell.cid);
}
