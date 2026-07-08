#pragma once

#include <stdint.h>

void mqttSettingsBegin();
const char *mqttSettingsHost();
uint16_t mqttSettingsPort();
bool mqttSettingsUseTls();
bool mqttSettingsSetBroker(const char *host, uint16_t port, bool useTls);
void mqttSettingsShow();
