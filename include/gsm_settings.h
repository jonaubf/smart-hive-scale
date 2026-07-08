#pragma once

#include "telemetry_payload.h"

void gsmSettingsBegin();
const char *gsmSettingsApn();
const char *gsmSettingsUser();
const char *gsmSettingsPass();
CellTowerInfo gsmSettingsCellTower();

bool gsmSettingsSetApn(const char *apn);
bool gsmSettingsSetUser(const char *user);
bool gsmSettingsSetPass(const char *pass);
bool gsmSettingsSetCredentials(const char *apn, const char *user, const char *pass);
bool gsmSettingsSetCellTower(const CellTowerInfo &cell);
void gsmSettingsShow();
