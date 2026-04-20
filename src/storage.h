#pragma once
#include <Arduino.h>

struct ScheduleCfg {
  uint16_t startHHMM = 630;   // e.g. 0630
  uint8_t  runMin    = 5;     // minutes
  bool     enabled   = true;
  uint8_t  daysMask  = 0x7F;  // bit0=Sun ... bit6=Sat
  bool     triggered = false; // daily trigger latch
};

struct DeviceCfg {
  String deviceName = "PortableWatering";
  String staSsid;
  String staPass;

  String apSsid  = "Watering-Setup";
  String apPass  = "water1234";

  String notifyEmail;
  bool   notifyLowTank  = true;
  bool   notifyErrors   = true;
  bool   notifyStatus   = false;

  ScheduleCfg morning;
  ScheduleCfg evening;

  float tankLevelMl = 55000.0f;
  float usageMl     = 0.0f;

  float flowMlPerSec = 7.5f;
  float minLevelMl   = 500.0f;
  float resetLevelMl = 55000.0f;
};

bool loadConfig(DeviceCfg& cfg);
bool saveConfig(const DeviceCfg& cfg);

void resetDailyTriggers(DeviceCfg& cfg);
