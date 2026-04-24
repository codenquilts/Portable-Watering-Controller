#pragma once
#include <Arduino.h>
#include <time.h>

enum class TimeSource : uint8_t {
  NONE   = 0,
  STORED = 1,
  NTP    = 2
};

void timeBegin();                 // offline-safe init (no WiFi required)
void timeLoop();                  // non-blocking upkeep
void timeOnWifiConnected();       // call once when WiFi transitions to connected
void timeSaveAnchorIfValid();     // persist clock anchor when valid
bool timeClearSavedAnchor();      // clear persisted clock anchor

bool timeIsValid();
TimeSource timeSource();

time_t timeNow();                 // stable time for the app (soft clock)
String timeNowStringHM();         // "HH:MM"
String timeNowStringHMS();        // "HH:MM:SS"
String timeFormatLocal(time_t epoch, const char* fmt = "%Y-%m-%d %H:%M:%S");
void timeSetTimezone(const char* tz); // set timezone and apply to NTP if connected
const char* timeSourceStr();      // "none" | "stored" | "ntp"
