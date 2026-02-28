#pragma once
#include <Arduino.h>
#include "storage.h"

enum class WifiModeState {
  STA_CONNECTED,
  AP_CAPTIVE
};

WifiModeState wifiBegin(DeviceCfg& cfg);
WifiModeState wifiGetState();
String wifiIpString();

// ---- WiFiManager Setup Mode (reboot-based) ----
bool wifiIsSetupMode();
void wifiEnterSetupMode();     // set flag + reboot
void wifiClearSetupModeFlag(); // clear flag after successful setup (optional)
void wifiResetCredentials();   // forget WiFi + reboot
