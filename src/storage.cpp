#include "storage.h"
#include <Preferences.h>

static Preferences prefs;

static constexpr float OLD_DEFAULT_FLOW_ML_PER_SEC = 7.5f;
static constexpr float DEFAULT_FLOW_ML_PER_SEC = 70.0f;

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

  cfg.mqttHost = getStringSafe("mqttHost", cfg.mqttHost);
  cfg.mqttPort = prefs.getUShort("mqttPort", cfg.mqttPort);
  cfg.mqttUser = getStringSafe("mqttUser", cfg.mqttUser);
  cfg.mqttPass = getStringSafe("mqttPass", cfg.mqttPass);

  cfg.smtpHost = getStringSafe("smtpHost", cfg.smtpHost);
  cfg.smtpPort = prefs.getUShort("smtpPort", cfg.smtpPort);
  cfg.smtpUser = getStringSafe("smtpUser", cfg.smtpUser);
  cfg.smtpPass = getStringSafe("smtpPass", cfg.smtpPass);
  cfg.smtpFrom = getStringSafe("smtpFrom", cfg.smtpFrom);
  cfg.smtpUseSsl = prefs.getBool("smtpSsl", cfg.smtpUseSsl);

  cfg.morning.startHHMM = prefs.getUShort("mStart", cfg.morning.startHHMM);
  cfg.morning.runMin    = prefs.getUChar("mRun",   cfg.morning.runMin);
  cfg.morning.runSec    = prefs.getUShort("mRunS",  (uint16_t)cfg.morning.runMin * 60U);
  cfg.morning.enabled   = prefs.getBool("mEn",     cfg.morning.enabled);
  cfg.morning.daysMask  = prefs.getUChar("mDays",  cfg.morning.daysMask);

  cfg.evening.startHHMM = prefs.getUShort("eStart", cfg.evening.startHHMM);
  cfg.evening.runMin    = prefs.getUChar("eRun",    cfg.evening.runMin);
  cfg.evening.runSec    = prefs.getUShort("eRunS",   (uint16_t)cfg.evening.runMin * 60U);
  cfg.evening.enabled   = prefs.getBool("eEn",      cfg.evening.enabled);
  cfg.evening.daysMask  = prefs.getUChar("eDays",   cfg.evening.daysMask);
  cfg.twoRelayVersion   = prefs.getBool("twoRelay", cfg.twoRelayVersion);

  cfg.tankLevelMl   = prefs.getFloat("tankLvl", cfg.tankLevelMl);
  cfg.usageMl       = prefs.getFloat("usage",   cfg.usageMl);
  cfg.flowMlPerSec  = prefs.getFloat("flow",    cfg.flowMlPerSec);
  if (cfg.flowMlPerSec == OLD_DEFAULT_FLOW_ML_PER_SEC) {
    cfg.flowMlPerSec = DEFAULT_FLOW_ML_PER_SEC;
  }
  cfg.returnFlowMlPerSec = prefs.getFloat("retFlow", cfg.returnFlowMlPerSec);
  cfg.minLevelMl    = prefs.getFloat("minLvl",  cfg.minLevelMl);
  cfg.resetLevelMl  = prefs.getFloat("rstLvl",  cfg.resetLevelMl);
  cfg.timeZone      = getStringSafe("timeZone", cfg.timeZone);

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

  putStringSafe("mqttHost", cfg.mqttHost);
  prefs.putUShort("mqttPort", cfg.mqttPort);
  putStringSafe("mqttUser", cfg.mqttUser);
  putStringSafe("mqttPass", cfg.mqttPass);

  putStringSafe("smtpHost", cfg.smtpHost);
  prefs.putUShort("smtpPort", cfg.smtpPort);
  putStringSafe("smtpUser", cfg.smtpUser);
  putStringSafe("smtpPass", cfg.smtpPass);
  putStringSafe("smtpFrom", cfg.smtpFrom);
  prefs.putBool("smtpSsl", cfg.smtpUseSsl);

  prefs.putUShort("mStart", cfg.morning.startHHMM);
  prefs.putUChar ("mRun",   cfg.morning.runMin);
  prefs.putUShort("mRunS",  cfg.morning.runSec);
  prefs.putBool  ("mEn",    cfg.morning.enabled);
  prefs.putUChar ("mDays",  cfg.morning.daysMask);

  prefs.putUShort("eStart", cfg.evening.startHHMM);
  prefs.putUChar ("eRun",   cfg.evening.runMin);
  prefs.putUShort("eRunS",  cfg.evening.runSec);
  prefs.putBool  ("eEn",    cfg.evening.enabled);
  prefs.putUChar ("eDays",  cfg.evening.daysMask);
  prefs.putBool  ("twoRelay", cfg.twoRelayVersion);

  prefs.putFloat("tankLvl", cfg.tankLevelMl);
  prefs.putFloat("usage",   cfg.usageMl);
  prefs.putFloat("flow",    cfg.flowMlPerSec);
  prefs.putFloat("retFlow", cfg.returnFlowMlPerSec);
  prefs.putFloat("minLvl",  cfg.minLevelMl);
  prefs.putFloat("rstLvl",  cfg.resetLevelMl);
  putStringSafe("timeZone", cfg.timeZone);

  prefs.end();
  return true;
}

void resetDailyTriggers(DeviceCfg& cfg) {
  cfg.morning.triggered = false;
  cfg.evening.triggered = false;
}



