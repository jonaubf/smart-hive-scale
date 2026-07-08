#include "telemetry_payload.h"

#include "wifi_manager.h"

namespace {

void jsonAppendEscapedString(String &json, const char *value) {
  json += '"';
  if (value != nullptr) {
    for (const char *p = value; *p != '\0'; p++) {
      const char ch = *p;
      if (ch == '"' || ch == '\\') {
        json += '\\';
      }
      json += ch;
    }
  }
  json += '"';
}

}  // namespace

String buildTelemetryJson(const char *deviceId, float weightKg, float stableKg,
                          float batteryV, int batteryPct, int gsmRssi,
                          const CellTowerInfo &cell, const WifiLinkInfo &wifi,
                          unsigned long txIntervalSec) {
  String json;
  json.reserve(384);
  json += "{";
  json += "\"device_id\":";
  jsonAppendEscapedString(json, deviceId);
  json += ",\"weight_kg\":";
  json += String(weightKg, 3);
  json += ",\"stable_kg\":";
  json += String(stableKg, 3);
  json += ",\"battery_v\":";
  json += String(batteryV, 3);
  json += ",\"battery_pct\":";
  json += String(batteryPct);
  json += ",\"rssi\":";
  json += String(gsmRssi);
  json += ",\"wifi_connected\":";
  json += wifi.connected ? "true" : "false";
  json += ",\"wifi_hostname\":";
  jsonAppendEscapedString(json, wifi.hostname);
  json += ",\"wifi_ip\":";
  jsonAppendEscapedString(json, wifi.ip);
  json += ",\"wifi_rssi\":";
  json += String(wifi.rssi);
  json += ",\"tx_interval_sec\":";
  json += String(txIntervalSec);
  json += ",\"cell_mcc\":";
  json += String(cell.mcc);
  json += ",\"cell_mnc\":";
  json += String(cell.mnc);
  json += ",\"cell_lac\":";
  json += String(cell.lac);
  json += ",\"cell_cid\":";
  json += String(cell.cid);
  json += "}";
  return json;
}
