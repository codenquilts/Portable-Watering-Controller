#include "time_mgr.h"
#include <Preferences.h>
#include "config.h"

static Preferences prefs;

// Soft-clock base (epoch at baseMs, then we add seconds via millis())
static bool     g_valid     = false;
static TimeSource g_src     = TimeSource::NONE;
static time_t   g_baseEpoch = 0;
static uint32_t g_baseMs    = 0;

static uint32_t g_lastSaveMs = 0;
static const uint32_t SAVE_EVERY_MS = 10UL * 60UL * 1000UL; // every 10 minutes

static bool epochLooksValid(time_t t) {
  // sanity threshold (~2020-09). Adjust if you want.
  return t > 1600000000;
}

const char* timeSourceStr() {
  switch (timeSource()) {
    case TimeSource::NTP:    return "ntp";
    case TimeSource::STORED: return "stored";
    default:                 return "none";
  }
}

static void setSoftClock(time_t epoch, TimeSource src) {
  g_baseEpoch = epoch;
  g_baseMs    = millis();
  g_valid     = epochLooksValid(epoch);
  g_src       = g_valid ? src : TimeSource::NONE;
}

String timeFormatLocal(time_t epoch, const char* fmt) {
  if (!epochLooksValid(epoch)) return String("-");
  struct tm tm;
  localtime_r(&epoch, &tm);
  char buf[32];
  strftime(buf, sizeof(buf), fmt, &tm);
  return String(buf);
}

static bool systemTimeLooksValid() {
  time_t now;
  time(&now);
  return epochLooksValid(now);
}

void timeBegin() {
  setenv("TZ", TZ_INFO, 1);
  tzset();

  // Try to start offline from stored anchor
  prefs.begin("time", true);
  uint32_t savedEpoch = prefs.getULong("t_epoch", 0);
  uint32_t savedMs    = prefs.getULong("t_ms", 0);
  prefs.end();

  if (savedEpoch != 0) {
    // Recreate "now" from saved anchor + elapsed millis since boot
    uint32_t nowMs = (uint32_t)millis();
    uint32_t elapsedMs = nowMs - savedMs;              // wrap-safe
    time_t estimatedNow = (time_t)(savedEpoch + (elapsedMs / 1000UL));
    setSoftClock(estimatedNow, TimeSource::STORED);
  } else {
    setSoftClock(0, TimeSource::NONE);
  }
}

void timeSaveAnchorIfValid() {
  if (!timeIsValid()) return;

  uint32_t epoch = (uint32_t)timeNow();   // use your soft clock
  uint32_t ms    = (uint32_t)millis();

  prefs.begin("time", false);
  prefs.putULong("t_epoch", epoch);
  prefs.putULong("t_ms", ms);
  prefs.end();
}

void timeOnWifiConnected() {
  // Non-blocking: NTP will sync in background. We detect validity in timeLoop().
  // Melbourne TZ (AEST/AEDT)
  configTzTime("AEST-10AEDT,M10.1.0/2,M4.1.0/3", "pool.ntp.org", "time.nist.gov");
}

void timeSetTimezone(const char* tz) {
  if (!tz || !*tz) return;
  // Update system timezone
  setenv("TZ", tz, 1);
  tzset();
  // If WiFi connected, also update NTP with new timezone
  if (systemTimeLooksValid()) {
    configTzTime(tz, "pool.ntp.org", "time.google.com");
  }
}

time_t timeNow() {
  if (!g_valid) return 0;
  uint32_t dt = (millis() - g_baseMs) / 1000;
  return g_baseEpoch + (time_t)dt;
}

bool timeIsValid() { return g_valid; }
TimeSource timeSource() { return g_src; }

static String fmtNow(const char* fmt) {
  if (!g_valid) return String("--:--");
  time_t t = timeNow();
  struct tm tm;
  localtime_r(&t, &tm);
  char buf[16];
  strftime(buf, sizeof(buf), fmt, &tm);
  return String(buf);
}

String timeNowStringHM()  { return fmtNow("%H:%M"); }
String timeNowStringHMS() { return fmtNow("%H:%M:%S"); }

void timeLoop() {
  static bool prevSystemValid = false;

  bool sysValid = systemTimeLooksValid();

  // If NTP has made system time valid, adopt it as our base
  if (sysValid) {
    time_t now;
    time(&now);

    if (!prevSystemValid || g_src != TimeSource::NTP || !g_valid) {
      // First time we've become valid (or switching to NTP)
      setSoftClock(now, TimeSource::NTP);
      timeSaveAnchorIfValid();            // <-- save immediately on NTP sync
      g_lastSaveMs = millis();            // reset periodic timer
    } else {
      // Optional: occasionally re-align soft clock to system time
      setSoftClock(now, TimeSource::NTP);
    }
  }

  prevSystemValid = sysValid;

  // Refresh stored anchor periodically whenever we have any valid time
  if (g_valid && (millis() - g_lastSaveMs) > SAVE_EVERY_MS) {
    g_lastSaveMs = millis();
    timeSaveAnchorIfValid();              // <-- your only periodic save now
  }
}
