#include "maintenance_portal.h"

#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <soc/rtc_cntl_reg.h>

#include "calibration.h"
#include "config.h"
#include "connectivity_mode.h"
#include "device_settings.h"
#include "gsm_settings.h"
#include "ip5306.h"
#include "modem_manager.h"
#include "mqtt_settings.h"
#include "radio_manager.h"
#include "weight_sensor.h"
#include "wifi_manager.h"

namespace {

WebServer server(80);
bool active = false;
bool staMode = false;
bool routesRegistered = false;
char apSsid[48] = "";

static const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Hive Scale setup</title>
<style>
body{font-family:system-ui,sans-serif;max-width:40rem;margin:1rem auto;padding:0 1rem;color:#222}
h1{font-size:1.4rem}h2{font-size:1.1rem;margin-top:1.5rem;border-bottom:1px solid #ddd;padding-bottom:.3rem}
label{display:block;margin:.6rem 0 .2rem;font-weight:600}
input,select,button{font:inherit;padding:.45rem .6rem;margin:.2rem 0}
input[type=text],input[type=password],input[type=number],select{width:100%;box-sizing:border-box}
button{cursor:pointer;background:#2d6a4f;color:#fff;border:0;border-radius:4px;margin-right:.4rem}
button.secondary{background:#555}
#weightBox{background:#f4f4f4;padding:.8rem;border-radius:6px;margin:.5rem 0}
#status{min-height:1.2rem;color:#b00020;margin:.5rem 0}
.row{display:flex;gap:.5rem;flex-wrap:wrap;align-items:end}
.row label{flex:1;min-width:8rem}
</style>
</head>
<body>
<h1>Smart Hive Scale setup</h1>
<p id="deviceId"></p>
<div id="status"></div>
<h2>Weight calibration</h2>
<div id="weightBox">Loading…</div>
<p>Offset: <span id="offset">-</span> · Scale: <span id="scale">-</span></p>
<button type="button" id="tareBtn">Tare (empty scale)</button>
<div class="row">
<label>Known weight (kg)<input type="number" id="knownKg" step="0.001" min="0.001"></label>
<button type="button" id="calBtn">Calibrate</button>
</div>
<h2>Operating mode</h2>
<label>Connectivity
<select id="mode"><option value="gsm">GSM (cellular)</option><option value="wifi">WiFi (home LAN)</option></select>
</label>
<label>Report interval (minutes)<input type="number" id="txInterval" min="1" max="1440"></label>
<h2>MQTT broker</h2>
<p>Same broker for WiFi and GSM. At home: LAN IP, port 1883, TLS off. In the field: public IP, port 8883, TLS on.</p>
<label>Host<input type="text" id="mqttHost" autocomplete="off" placeholder="192.168.1.100 or 203.0.113.1"></label>
<label>Port<input type="number" id="mqttPort" min="1" max="65535"></label>
<label><input type="checkbox" id="mqttTls"> Use TLS (recommended for cellular / internet)</label>
<h2>WiFi client</h2>
<label>SSID<input type="text" id="wifiSsid" autocomplete="off"></label>
<label>Password<input type="password" id="wifiPass" autocomplete="off"></label>
<h2>GSM / SIM</h2>
<label>APN<input type="text" id="gsmApn"></label>
<label>Username<input type="text" id="gsmUser"></label>
<label>Password<input type="password" id="gsmPass"></label>
<div class="row">
<label>MCC<input type="number" id="cellMcc"></label>
<label>MNC<input type="number" id="cellMnc"></label>
<label>LAC<input type="number" id="cellLac"></label>
<label>CID<input type="number" id="cellCid"></label>
</div>
<h2>Firmware update</h2>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin" required>
<button type="submit" class="secondary">Upload firmware</button>
</form>
<h2>Save</h2>
<button type="button" id="saveBtn">Save settings and reboot</button>
<script>
const $=id=>document.getElementById(id);
const status=t=>{$("status").textContent=t||""};
function fillForm(s){
$("deviceId").textContent="Device: "+s.device_id;
$("mode").value=s.mode;
$("txInterval").value=s.tx_interval_min;
$("mqttHost").value=s.mqtt_host||"";
$("mqttPort").value=s.mqtt_port;
$("mqttTls").checked=!!s.mqtt_tls;
$("wifiSsid").value=s.wifi_ssid||"";
$("wifiPass").value=s.wifi_pass||"";
$("gsmApn").value=s.gsm_apn||"";
$("gsmUser").value=s.gsm_user||"";
$("gsmPass").value=s.gsm_pass||"";
$("cellMcc").value=s.cell_mcc;
$("cellMnc").value=s.cell_mnc;
$("cellLac").value=s.cell_lac;
$("cellCid").value=s.cell_cid;
$("offset").textContent=s.offset;
$("scale").textContent=s.scale;
}
async function loadSettings(){fillForm(await (await fetch("/api/settings")).json());}
async function refreshWeight(){
const w=await (await fetch("/api/weight")).json();
if(!w.ok){$("weightBox").textContent="Sensor not ready";return;}
let t="Raw: "+w.raw;
if(w.weight_kg!==undefined)t+=" · Weight: "+w.weight_kg+" kg";
$("weightBox").textContent=t;
}
async function postAction(url,body){
const r=await fetch(url,{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body});
const j=await r.json();
if(!j.ok)throw new Error(j.error||"failed");
return j;
}
$("tareBtn").onclick=async()=>{status("Taring…");try{const j=await postAction("/api/tare","");$("offset").textContent=j.offset;status("Tare OK");await refreshWeight();}catch(e){status(e.message)}};
$("calBtn").onclick=async()=>{const kg=$("knownKg").value;if(!kg){status("Enter known weight");return;}status("Calibrating…");try{const j=await postAction("/api/calibrate","known_kg="+encodeURIComponent(kg));$("scale").textContent=j.scale;status("Calibration OK");await refreshWeight();}catch(e){status(e.message)}};
$("saveBtn").onclick=async()=>{status("Saving…");const p=new URLSearchParams();p.set("mode",$("mode").value);p.set("tx_interval_min",$("txInterval").value);p.set("mqtt_host",$("mqttHost").value);p.set("mqtt_port",$("mqttPort").value);p.set("mqtt_tls",$("mqttTls").checked?"1":"0");p.set("wifi_ssid",$("wifiSsid").value);p.set("wifi_pass",$("wifiPass").value);p.set("gsm_apn",$("gsmApn").value);p.set("gsm_user",$("gsmUser").value);p.set("gsm_pass",$("gsmPass").value);p.set("cell_mcc",$("cellMcc").value);p.set("cell_mnc",$("cellMnc").value);p.set("cell_lac",$("cellLac").value);p.set("cell_cid",$("cellCid").value);try{await postAction("/api/save",p.toString());status("Rebooting…");}catch(e){status(e.message)}};
loadSettings();refreshWeight();setInterval(refreshWeight,2000);
</script>
</body>
</html>)HTML";

void jsonEscapeAppend(String &out, const char *value) {
  if (value == nullptr) {
    return;
  }
  for (const char *p = value; *p != '\0'; p++) {
    const char ch = *p;
    if (ch == '"' || ch == '\\') {
      out += '\\';
    }
    out += ch;
  }
}

void jsonKeyString(String &json, const char *key, const char *value) {
  json += '"';
  json += key;
  json += "\":\"";
  jsonEscapeAppend(json, value);
  json += '"';
}

String buildSettingsJson() {
  const unsigned long txSec = settingsTxIntervalSec();
  const uint16_t txMin = static_cast<uint16_t>(txSec / 60UL);
  const CellTowerInfo cell = gsmSettingsCellTower();

  String json;
  json.reserve(512);
  json += '{';
  jsonKeyString(json, "device_id", DEVICE_ID);
  json += ',';
  jsonKeyString(json, "mode", connectivityModeName(connectivityMode()));
  json += ',';
  jsonKeyString(json, "wifi_ssid", connectivityWifiSsid());
  json += ',';
  jsonKeyString(json, "wifi_pass", connectivityWifiPassword());
  json += ',';
  jsonKeyString(json, "gsm_apn", gsmSettingsApn());
  json += ',';
  jsonKeyString(json, "gsm_user", gsmSettingsUser());
  json += ',';
  jsonKeyString(json, "gsm_pass", gsmSettingsPass());
  json += ',';
  jsonKeyString(json, "mqtt_host", mqttSettingsHost());
  json += ',';
  json += "\"mqtt_port\":";
  json += String(mqttSettingsPort());
  json += ',';
  json += "\"mqtt_tls\":";
  json += mqttSettingsUseTls() ? "true" : "false";
  json += ',';
  json += "\"tx_interval_min\":";
  json += String(txMin);
  json += ',';
  json += "\"cell_mcc\":";
  json += String(cell.mcc);
  json += ',';
  json += "\"cell_mnc\":";
  json += String(cell.mnc);
  json += ',';
  json += "\"cell_lac\":";
  json += String(cell.lac);
  json += ',';
  json += "\"cell_cid\":";
  json += String(cell.cid);
  json += ',';
  json += "\"offset\":";
  json += String(calibrationOffset());
  json += ',';
  json += "\"scale\":";
  json += String(calibrationScale(), 3);
  json += ',';
  json += "\"calibrated\":";
  json += calibrationIsReady() ? "true" : "false";
  json += '}';
  return json;
}

String buildWeightJson() {
  WeightSensorReading reading = weightSensorReadRaw(SCALE_RAW_SAMPLES);
  String json = "{\"ok\":";
  if (!reading.ok) {
    json += "false}";
    return json;
  }

  json += "true,\"raw\":";
  json += String(reading.raw);
  if (calibrationIsReady()) {
    json += ",\"weight_kg\":";
    json += String(calibrationWeightKg(reading.raw), 3);
  }
  json += '}';
  return json;
}

void handleRoot() { server.send_P(200, "text/html", PORTAL_HTML); }

void handleCaptiveProbe() {
  // Phones probe random URLs on join; serve a tiny redirect instead of 404 + heavy logging.
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void handleSettingsGet() { server.send(200, "application/json", buildSettingsJson()); }

void handleWeightGet() { server.send(200, "application/json", buildWeightJson()); }

void handleTarePost() {
  if (calibrationTare()) {
    String response = "{\"ok\":true,\"offset\":";
    response += String(calibrationOffset());
    response += '}';
    server.send(200, "application/json", response);
    return;
  }
  server.send(500, "application/json", "{\"ok\":false,\"error\":\"tare_failed\"}");
}

void handleCalibratePost() {
  const float knownKg = server.arg("known_kg").toFloat();
  if (knownKg > 0.0f && calibrationCalibrate(knownKg)) {
    String response = "{\"ok\":true,\"scale\":";
    response += String(calibrationScale(), 3);
    response += '}';
    server.send(200, "application/json", response);
    return;
  }
  server.send(500, "application/json", "{\"ok\":false,\"error\":\"cal_failed\"}");
}

bool applySettingsFromRequest(String &errorOut) {
  ConnectivityMode newMode;
  const String modeArg = server.arg("mode");
  if (!connectivityParseMode(modeArg.c_str(), &newMode)) {
    errorOut = "invalid_mode";
    return false;
  }

  const String wifiSsid = server.arg("wifi_ssid");
  const String wifiPass = server.arg("wifi_pass");
  if (newMode == ConnectivityMode::WifiSta && wifiSsid.length() == 0) {
    errorOut = "wifi_ssid_required";
    return false;
  }
  if (wifiSsid.length() > 0 &&
      !connectivitySetWifiCredentials(wifiSsid.c_str(), wifiPass.c_str())) {
    errorOut = "wifi_cred_invalid";
    return false;
  }

  const String apn = server.arg("gsm_apn");
  if (apn.length() == 0 || !gsmSettingsSetCredentials(apn.c_str(), server.arg("gsm_user").c_str(),
                                                      server.arg("gsm_pass").c_str())) {
    errorOut = "gsm_invalid";
    return false;
  }

  const int txMin = server.arg("tx_interval_min").toInt();
  if (txMin < 1 || txMin > 1440 || !settingsSetTxIntervalMin(static_cast<uint16_t>(txMin))) {
    errorOut = "tx_interval_invalid";
    return false;
  }

  const String mqttHost = server.arg("mqtt_host");
  const int mqttPort = server.arg("mqtt_port").toInt();
  const bool mqttTls = server.arg("mqtt_tls") == "1" || server.arg("mqtt_tls") == "true";
  if (!mqttSettingsSetBroker(mqttHost.c_str(), static_cast<uint16_t>(mqttPort), mqttTls)) {
    errorOut = "mqtt_invalid";
    return false;
  }

  const CellTowerInfo cell{server.arg("cell_mcc").toInt(), server.arg("cell_mnc").toInt(),
                         server.arg("cell_lac").toInt(), server.arg("cell_cid").toInt()};
  gsmSettingsSetCellTower(cell);
  connectivitySetMode(newMode);
  return true;
}

void handleSavePost() {
  String error;
  if (!applySettingsFromRequest(error)) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"" + error + "\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
  delay(300);
  ESP.restart();
}

void handleUpdateDone() {
  server.sendHeader("Connection", "close");
  if (Update.hasError()) {
    server.send(500, "text/plain", "Update failed");
    return;
  }
  server.send(200, "text/plain", "OK — rebooting");
  delay(500);
  ESP.restart();
}

void handleUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("OTA start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) {
      Update.printError(Serial);
    } else {
      Serial.printf("OTA complete: %u bytes\n", upload.totalSize);
    }
  }
}

}  // namespace

void registerPortalRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/generate_204", HTTP_GET, handleCaptiveProbe);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptiveProbe);
  server.on("/fwlink", HTTP_GET, handleCaptiveProbe);
  server.on("/api/settings", HTTP_GET, handleSettingsGet);
  server.on("/api/weight", HTTP_GET, handleWeightGet);
  server.on("/api/tare", HTTP_POST, handleTarePost);
  server.on("/api/calibrate", HTTP_POST, handleCalibratePost);
  server.on("/api/save", HTTP_POST, handleSavePost);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpload);
  server.onNotFound(handleCaptiveProbe);
}

void maintenancePortalBegin(bool forceAp) {
  if (active) {
    if (!forceAp || !staMode) {
      Serial.println(F("Portal already active"));
      if (staMode) {
        Serial.printf("Open http://%s\n", WiFi.localIP().toString().c_str());
      } else {
        Serial.println(F("Open http://192.168.4.1"));
      }
      return;
    }
    // Explicit request while the STA portal runs: switch to standalone AP so
    // the user can reconfigure WiFi even without the current network.
    Serial.println(F("Switching portal to WiFi AP mode..."));
    server.stop();
    active = false;
    wifiManagerDisconnect();
  }

  if (!routesRegistered) {
    registerPortalRoutes();
    routesRegistered = true;
  }

  if (!forceAp && connectivityMode() == ConnectivityMode::WifiSta && wifiManagerIsConnected()) {
    server.begin();
    staMode = true;
    active = true;
    Serial.println();
    Serial.println(F("=== Config portal (WiFi client) ==="));
    Serial.printf("Open http://%s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Hostname: %s\n", WiFi.getHostname());
    return;
  }

  staMode = false;
  modemManagerPowerOff();
  setCpuFrequencyMhz(160);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  radioPowerUpForPortal();
  if (!ip5306EnsureBoostKeepOn()) {
    Serial.println(F("WARN IP5306 boost keep-on failed"));
  }
  snprintf(apSsid, sizeof(apSsid), "beekpr-%s", DEVICE_ID);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_2dBm);
  WiFi.mode(WIFI_AP);
  // One client, low TX — phone is beside the hive; reduces USB brownout on connect.
  if (!WiFi.softAP(apSsid, WIFI_AP_PASSWORD, 1, false, 1)) {
    Serial.println(F("ERR failed to start WiFi AP"));
    return;
  }

  server.begin();
  active = true;
  Serial.println();
  Serial.println(F("=== Config portal (WiFi AP) ==="));
  Serial.printf("AP SSID: %s\n", apSsid);
  Serial.printf("AP password: %s\n", WIFI_AP_PASSWORD);
  Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.println(F("Open http://192.168.4.1"));
}

void maintenancePortalLoop() {
  if (!active) {
    return;
  }
  server.handleClient();
}

bool maintenancePortalIsActive() { return active; }

bool maintenancePortalIsStaMode() { return active && staMode; }
