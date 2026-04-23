#pragma once
#include <Arduino.h>
#include "storage.h"

struct RuntimeState {
  bool bootReady = false;
  bool pumpOn = false;
  bool lowTankEmailSent = false;

  // Used to avoid retriggering schedules multiple times in the same minute
  uint32_t lastMinuteKey = 0; // YYYYMMDDHHMM-like key

  // ---- RAM log (not persisted) ----
  uint32_t lastStartTs = 0;    // epoch seconds (0 if unknown)
  uint32_t lastStopTs  = 0;    // epoch seconds (0 if still running / unknown)
  uint32_t lastRunS    = 0;    // seconds (0 if unknown)
  uint32_t pumpStartMs = 0;    // monotonic millis at relay ON
  uint32_t pumpStopAtMs = 0;   // monotonic millis deadline
  uint16_t requestedRunS = 0;  // requested relay runtime in seconds
  uint8_t  activeZone = 0;     // 0=none, 1=relay 1, 2=relay 2
  char     lastReason[16] = "BOOT";
};

void schedulerBegin();
void schedulerLoop(DeviceCfg& cfg, RuntimeState& st);

// New API (reason logging)
void pumpStartForMinutes(RuntimeState& st, uint8_t minutes, const char* reason);
void pumpStartForSeconds(RuntimeState& st, uint16_t seconds, uint8_t zone, const char* reason);
void pumpStop(RuntimeState& st);
void pumpStopWithReason(RuntimeState& st, const char* reason);
uint32_t pumpRemainingSeconds(const RuntimeState& st);

// Back-compat wrapper (older call sites)
inline void pumpStartForMinutes(RuntimeState& st, uint8_t minutes) {
  pumpStartForMinutes(st, minutes, "UNKNOWN");
}
