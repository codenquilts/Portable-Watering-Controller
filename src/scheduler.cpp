// scheduler.cpp — clean replacement (non-blocking time via time_mgr, reason-aware logs)

#include "scheduler.h"
#include "config.h"
#include "wifi_mgr.h"
#include "led_status.h"
#include "time_mgr.h"

#include <Arduino.h>
#include <time.h>

// ---------- helpers ----------
static void ledSetForNetwork() {
  ledSetMode(wifiGetState() == WifiModeState::STA_CONNECTED
               ? LedMode::HEARTBEAT
               : LedMode::AP_DOUBLE);
}

static uint16_t nowHHMM(const struct tm& t) {
  return (uint16_t)(t.tm_hour * 100 + t.tm_min);
}

static bool scheduleActiveOnDay(const ScheduleCfg& sched, const struct tm& t) {
  if (t.tm_wday < 0 || t.tm_wday > 6) return false;
  return (sched.daysMask & (1U << t.tm_wday)) != 0;
}

static uint32_t minuteKey(const struct tm& t) {
  // YYYYMMDDHHMM
  uint32_t y  = (uint32_t)(t.tm_year + 1900);
  uint32_t mo = (uint32_t)(t.tm_mon + 1);
  uint32_t d  = (uint32_t)t.tm_mday;
  uint32_t hh = (uint32_t)t.tm_hour;
  uint32_t mm = (uint32_t)t.tm_min;
  return y * 100000000UL + mo * 1000000UL + d * 10000UL + hh * 100UL + mm;
}

static void setReason(RuntimeState& st, const char* reason) {
  if (!reason || !*reason) reason = "UNKNOWN";
  strncpy(st.lastReason, reason, sizeof(st.lastReason) - 1);
  st.lastReason[sizeof(st.lastReason) - 1] = 0;
}

// ---------- lifecycle ----------
void schedulerBegin() {
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, RELAY_OFF_LEVEL);
}

// 3-arg version (reason-aware)
void pumpStartForMinutes(RuntimeState& st, uint8_t minutes, const char* reason) {
  pinMode(PIN_RELAY, OUTPUT);

  digitalWrite(PIN_RELAY, RELAY_ON_LEVEL);
  st.pumpOn = true;

  // LED: pump active
  ledSetMode(LedMode::SOLID_ON);

  if (minutes == 0) minutes = 1;
  if (minutes > RUN_MAX_MINUTES) minutes = RUN_MAX_MINUTES;

  const uint32_t nowMs = millis();
  const uint32_t runMs = (uint32_t)minutes * 60UL * 1000UL;
  st.pumpStartMs = nowMs;
  st.pumpStopAtMs = nowMs + runMs;
  st.requestedRunS = (uint16_t)minutes * 60U;

  // ----- RAM log -----
  setReason(st, reason);
  st.lastStartTs = timeIsValid() ? (uint32_t)timeNow() : 0;
  st.lastStopTs  = 0;
  st.lastRunS    = 0;

  Serial.printf("PUMP START: reason=%s requested=%us stop_in_ms=%lu\n",
                st.lastReason,
                (unsigned)st.requestedRunS,
                (unsigned long)runMs);
}

void pumpStop(RuntimeState& st) {
  pinMode(PIN_RELAY, OUTPUT);

  digitalWrite(PIN_RELAY, RELAY_OFF_LEVEL);
  st.pumpOn = false;

  // LED: back to network indication
  ledSetForNetwork();

  // ----- RAM log -----
  const uint32_t stopMs = millis();
  uint32_t stopTs = timeIsValid() ? (uint32_t)timeNow() : 0;
  st.lastStopTs = stopTs;

  if (st.pumpStartMs != 0) {
    st.lastRunS = (stopMs - st.pumpStartMs + 500UL) / 1000UL;
  } else if (st.lastStartTs && stopTs && stopTs >= st.lastStartTs) {
    st.lastRunS = stopTs - st.lastStartTs;
  }

  Serial.printf("PUMP STOP: reason=%s actual=%lus requested=%us remaining=%lus\n",
                st.lastReason,
                (unsigned long)st.lastRunS,
                (unsigned)st.requestedRunS,
                (unsigned long)pumpRemainingSeconds(st));

  st.pumpStartMs = 0;
  st.pumpStopAtMs = 0;
  st.requestedRunS = 0;
}

void pumpStopWithReason(RuntimeState& st, const char* reason) {
  setReason(st, reason);
  pumpStop(st);
}

uint32_t pumpRemainingSeconds(const RuntimeState& st) {
  if (!st.pumpOn || st.pumpStopAtMs == 0) return 0;
  const int32_t remainingMs = (int32_t)(st.pumpStopAtMs - millis());
  if (remainingMs <= 0) return 0;
  return ((uint32_t)remainingMs + 999UL) / 1000UL;
}

// ---------- schedule start ----------
static void tryStartSchedule(DeviceCfg& cfg, RuntimeState& st, const struct tm& t, uint16_t hhmm, uint32_t mkey) {
  if (st.pumpOn) return;
  if (!st.bootReady) return;

  // Tank safety (consume the minute so we don't spam attempts)
  if (cfg.tankLevelMl <= cfg.minLevelMl) {
    st.lastMinuteKey = mkey;
    return;
  }

  // Morning
  if (cfg.morning.enabled &&
      scheduleActiveOnDay(cfg.morning, t) &&
      hhmm == cfg.morning.startHHMM) {
    st.lastMinuteKey = mkey;
    Serial.printf("SCHEDULE START: Morning %04u for %u min\n",
                  cfg.morning.startHHMM, cfg.morning.runMin);
    pumpStartForMinutes(st, cfg.morning.runMin, "MORNING");
    return;
  }

  // Evening
  if (cfg.evening.enabled &&
      scheduleActiveOnDay(cfg.evening, t) &&
      hhmm == cfg.evening.startHHMM) {
    st.lastMinuteKey = mkey;
    Serial.printf("SCHEDULE START: Evening %04u for %u min\n",
                  cfg.evening.startHHMM, cfg.evening.runMin);
    pumpStartForMinutes(st, cfg.evening.runMin, "EVENING");
    return;
  }

  st.lastMinuteKey = mkey;
}

// ---------- main loop ----------
void schedulerLoop(DeviceCfg& cfg, RuntimeState& st) {
  // Auto stop pump
  if (st.pumpOn && st.pumpStopAtMs != 0) {
    if ((int32_t)(millis() - st.pumpStopAtMs) >= 0) {
      // mark why it stopped (only if it wasn't already a known type)
      if (strncmp(st.lastReason, "MORNING", 7) != 0 &&
          strncmp(st.lastReason, "EVENING", 7) != 0 &&
          strncmp(st.lastReason, "MANUAL", 6)  != 0 &&
          strncmp(st.lastReason, "MQTT", 4)    != 0) {
        setReason(st, "TIMEOUT");
      }
      pumpStop(st);
    }
  }

  // Check schedules once per minute (non-blocking)
  if (!timeIsValid()) return;

  time_t now = timeNow();
  struct tm t;
  localtime_r(&now, &t);

  const uint16_t hhmm = nowHHMM(t);
  const uint32_t mkey = minuteKey(t);

  if (st.lastMinuteKey == mkey) return;
  tryStartSchedule(cfg, st, t, hhmm, mkey);
}
