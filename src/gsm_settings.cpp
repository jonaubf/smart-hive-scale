#include "gsm_settings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "config.h"

namespace {

constexpr const char *NVS_NAMESPACE = "beekpr_gsm";
constexpr const char *KEY_APN = "apn";
constexpr const char *KEY_USER = "user";
constexpr const char *KEY_PASS = "pass";
constexpr const char *KEY_CELL_MCC = "cell_mcc";
constexpr const char *KEY_CELL_MNC = "cell_mnc";
constexpr const char *KEY_CELL_LAC = "cell_lac";
constexpr const char *KEY_CELL_CID = "cell_cid";
constexpr size_t APN_MAX = 64;
constexpr size_t USER_MAX = 32;
constexpr size_t PASS_MAX = 32;

Preferences prefs;
char apn[APN_MAX + 1] = "";
char user[USER_MAX + 1] = "";
char pass[PASS_MAX + 1] = "";
CellTowerInfo cellTower{255, 255, -1, -1};

void loadString(const char *key, char *dest, size_t destMax, const char *fallback) {
  const size_t len = prefs.getBytesLength(key);
  if (len > 0 && len <= destMax) {
    prefs.getBytes(key, dest, len + 1);
    return;
  }
  strncpy(dest, fallback, destMax);
  dest[destMax] = '\0';
}

bool storeString(const char *key, const char *value, size_t maxLen) {
  if (value == nullptr) {
    return false;
  }
  const size_t len = strlen(value);
  if (len > maxLen) {
    return false;
  }
  prefs.putBytes(key, value, len + 1);
  return true;
}

}  // namespace

void gsmSettingsBegin() {
  prefs.begin(NVS_NAMESPACE, false);
  loadString(KEY_APN, apn, APN_MAX, GSM_APN);
  loadString(KEY_USER, user, USER_MAX, GSM_USER);
  loadString(KEY_PASS, pass, PASS_MAX, GSM_PASS);
  cellTower.mcc = prefs.getInt(KEY_CELL_MCC, 255);
  cellTower.mnc = prefs.getInt(KEY_CELL_MNC, 255);
  cellTower.lac = prefs.getInt(KEY_CELL_LAC, -1);
  cellTower.cid = prefs.getInt(KEY_CELL_CID, -1);
}

const char *gsmSettingsApn() { return apn; }

const char *gsmSettingsUser() { return user; }

const char *gsmSettingsPass() { return pass; }

CellTowerInfo gsmSettingsCellTower() { return cellTower; }

bool gsmSettingsSetApn(const char *value) {
  if (!storeString(KEY_APN, value, APN_MAX)) {
    return false;
  }
  strncpy(apn, value, APN_MAX);
  apn[APN_MAX] = '\0';
  return true;
}

bool gsmSettingsSetUser(const char *value) {
  if (!storeString(KEY_USER, value, USER_MAX)) {
    return false;
  }
  strncpy(user, value, USER_MAX);
  user[USER_MAX] = '\0';
  return true;
}

bool gsmSettingsSetPass(const char *value) {
  if (!storeString(KEY_PASS, value, PASS_MAX)) {
    return false;
  }
  strncpy(pass, value, PASS_MAX);
  pass[PASS_MAX] = '\0';
  return true;
}

bool gsmSettingsSetCredentials(const char *apnValue, const char *userValue,
                               const char *passValue) {
  if (apnValue == nullptr || userValue == nullptr || passValue == nullptr) {
    return false;
  }
  if (strlen(apnValue) == 0 || strlen(apnValue) > APN_MAX || strlen(userValue) > USER_MAX ||
      strlen(passValue) > PASS_MAX) {
    return false;
  }
  if (!gsmSettingsSetApn(apnValue) || !gsmSettingsSetUser(userValue) ||
      !gsmSettingsSetPass(passValue)) {
    return false;
  }
  return true;
}

bool gsmSettingsSetCellTower(const CellTowerInfo &cell) {
  cellTower = cell;
  prefs.putInt(KEY_CELL_MCC, cell.mcc);
  prefs.putInt(KEY_CELL_MNC, cell.mnc);
  prefs.putInt(KEY_CELL_LAC, cell.lac);
  prefs.putInt(KEY_CELL_CID, cell.cid);
  return true;
}

void gsmSettingsShow() {
  Serial.printf("gsm_apn=%s gsm_user=%s\n", apn, user);
  Serial.printf("cell_mcc=%d cell_mnc=%d cell_lac=%d cell_cid=%d\n", cellTower.mcc,
                cellTower.mnc, cellTower.lac, cellTower.cid);
}
