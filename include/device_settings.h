#pragma once

#include <stdint.h>

void settingsBegin();
unsigned long settingsTxIntervalSec();
bool settingsSetTxIntervalMin(uint16_t minutes);
void settingsShow();
