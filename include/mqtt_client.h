#pragma once

#include <Arduino.h>

bool mqttClientRunTlsSocketTest(unsigned long networkTimeoutMs,
                                unsigned long tlsTimeoutMs);
bool mqttClientRunPublishTest(unsigned long networkTimeoutMs,
                              unsigned long mqttConnectTimeoutMs);
