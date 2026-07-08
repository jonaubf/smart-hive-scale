#pragma once

#include <stdint.h>

enum class ConnectivityMode : uint8_t {
  Gsm = 0,
  WifiSta = 1,
};

void connectivityBegin();
ConnectivityMode connectivityMode();
const char *connectivityModeName(ConnectivityMode mode);
bool connectivitySetMode(ConnectivityMode mode);
bool connectivityParseMode(const char *name, ConnectivityMode *outMode);

bool connectivitySetWifiCredentials(const char *ssid, const char *password);
const char *connectivityWifiSsid();
const char *connectivityWifiPassword();
bool connectivityHasWifiCredentials();
void connectivityShow();
