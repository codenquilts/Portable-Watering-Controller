#pragma once
#include <Arduino.h>
#include "storage.h"

struct RuntimeState {
  bool bootReady = false;
  bool pumpOn = false;

  // Used to avoid retriggering schedules multiple times in the same minute
  uint32_t lastMinuteKey = 0; // YYYYMMDDHHMM-like key

  // ---- RAM log (not persisted) ----
  uint32_t lastStartTs = 0;    // epoch seconds (0 if unknown)
  uint32_t lastStopTs  = 0;    // epoch seconds (0 if still running / unknown)
  uint32_t lastRunS    = 0;    // seconds (0 if unknown)
  char     lastReason[16] = "BOOT";
};

void schedulerBegin();
void schedulerLoop(DeviceCfg& cfg, RuntimeState& st);

// New API (reason logging)
void pumpStartForMinutes(RuntimeState& st, uint8_t minutes, const char* reason);
void pumpStop(RuntimeState& st);

// Back-compat wrapper (older call sites)
inline void pumpStartForMinutes(RuntimeState& st, uint8_t minutes) {
  pumpStartForMinutes(st, minutes, "UNKNOWN");
}