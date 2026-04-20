#include "storage.h"
#include <Preferences.h>

static Preferences prefs;

static void putStringSafe(const char* key, const String& v) {
  prefs.putString(key, v);
}
static String getStringSafe(const char* key, const String& def="") {
  return prefs.getString(key, def.c_str());
}

bool loadConfig(DeviceCfg& cfg) {
  if (!prefs.begin("watering", true)) return false;

  cfg.deviceName = getStringSafe("devName", cfg.deviceName);
  cfg.staSsid    = getStringSafe("staSsid", "");
  cfg.staPass    = getStringSafe("staPass", "");

  cfg.apSsid     = getStringSafe("apSsid", cfg.apSsid);
  cfg.apPass     = getStringSafe("apPass", cfg.apPass);

  cfg.notifyEmail    = getStringSafe("notifyEmail", cfg.notifyEmail);
  cfg.notifyLowTank  = prefs.getBool("notifyLow", cfg.notifyLowTank);
  cfg.notifyErrors   = prefs.getBool("notifyErr", cfg.notifyErrors);
  cfg.notifyStatus   = prefs.getBool("notifyStat", cfg.notifyStatus);

  cfg.morning.startHHMM = prefs.getUShort("mStart", cfg.morning.startHHMM);
  cfg.morning.runMin    = prefs.getUChar("mRun",   cfg.morning.runMin);
  cfg.morning.enabled   = prefs.getBool("mEn",     cfg.morning.enabled);
  cfg.morning.daysMask  = prefs.getUChar("mDays",  cfg.morning.daysMask);

  cfg.evening.startHHMM = prefs.getUShort("eStart", cfg.evening.startHHMM);
  cfg.evening.runMin    = prefs.getUChar("eRun",    cfg.evening.runMin);
  cfg.evening.enabled   = prefs.getBool("eEn",      cfg.evening.enabled);
  cfg.evening.daysMask  = prefs.getUChar("eDays",   cfg.evening.daysMask);

  cfg.tankLevelMl   = prefs.getFloat("tankLvl", cfg.tankLevelMl);
  cfg.usageMl       = prefs.getFloat("usage",   cfg.usageMl);
  cfg.flowMlPerSec  = prefs.getFloat("flow",    cfg.flowMlPerSec);
  cfg.minLevelMl    = prefs.getFloat("minLvl",  cfg.minLevelMl);
  cfg.resetLevelMl  = prefs.getFloat("rstLvl",  cfg.resetLevelMl);

  prefs.end();
  return true;
}

bool saveConfig(const DeviceCfg& cfg) {
  if (!prefs.begin("watering", false)) return false;

  putStringSafe("devName", cfg.deviceName);
  putStringSafe("staSsid", cfg.staSsid);
  putStringSafe("staPass", cfg.staPass);

  putStringSafe("apSsid", cfg.apSsid);
  putStringSafe("apPass", cfg.apPass);

  putStringSafe("notifyEmail", cfg.notifyEmail);
  prefs.putBool("notifyLow", cfg.notifyLowTank);
  prefs.putBool("notifyErr", cfg.notifyErrors);
  prefs.putBool("notifyStat", cfg.notifyStatus);

  prefs.putUShort("mStart", cfg.morning.startHHMM);
  prefs.putUChar ("mRun",   cfg.morning.runMin);
  prefs.putBool  ("mEn",    cfg.morning.enabled);
  prefs.putUChar ("mDays",  cfg.morning.daysMask);

  prefs.putUShort("eStart", cfg.evening.startHHMM);
  prefs.putUChar ("eRun",   cfg.evening.runMin);
  prefs.putBool  ("eEn",    cfg.evening.enabled);
  prefs.putUChar ("eDays",  cfg.evening.daysMask);

  prefs.putFloat("tankLvl", cfg.tankLevelMl);
  prefs.putFloat("usage",   cfg.usageMl);
  prefs.putFloat("flow",    cfg.flowMlPerSec);
  prefs.putFloat("minLvl",  cfg.minLevelMl);
  prefs.putFloat("rstLvl",  cfg.resetLevelMl);

  prefs.end();
  return true;
}

void resetDailyTriggers(DeviceCfg& cfg) {
  cfg.morning.triggered = false;
  cfg.evening.triggered = false;
}
