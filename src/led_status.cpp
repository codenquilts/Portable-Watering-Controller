#include "led_status.h"

static uint8_t g_pin = 255;
static bool    g_activeLow = true;
static LedMode g_mode = LedMode::OFF;

static uint32_t g_tNext = 0;
static bool     g_on = false;

// Pattern state
static uint8_t  g_step = 0;

static void writeLed(bool on) {
  g_on = on;
  if (g_pin == 255) return;
  // activeLow means LED is ON when pin is LOW
  digitalWrite(g_pin, (g_activeLow ? (on ? LOW : HIGH) : (on ? HIGH : LOW)));
}

void ledBegin(uint8_t pin, bool activeLow) {
  g_pin = pin;
  g_activeLow = activeLow;
  pinMode(g_pin, OUTPUT);
  writeLed(false);
  g_tNext = millis();
  g_step = 0;
  g_mode = LedMode::OFF;
}

void ledSetMode(LedMode mode) {
  if (mode == g_mode) return;
  g_mode = mode;
  g_step = 0;
  g_tNext = millis();
  // set immediate output for simple modes
  if (g_mode == LedMode::OFF)      writeLed(false);
  if (g_mode == LedMode::SOLID_ON) writeLed(true);
}

LedMode ledGetMode() { return g_mode; }

void ledSetError(bool isError) {
  if (isError) ledSetMode(LedMode::ERROR_TRIPLE);
}

void ledTick() {
  if (g_pin == 255) return;

  // simple modes already set
  if (g_mode == LedMode::OFF || g_mode == LedMode::SOLID_ON) return;

  uint32_t now = millis();
  if ((int32_t)(now - g_tNext) < 0) return;

  // Each mode drives a small state machine
  switch (g_mode) {
    case LedMode::BOOT_FAST:
      // 120ms on/off
      writeLed(!g_on);
      g_tNext = now + 120;
      break;

    case LedMode::HEARTBEAT:
      // short pulse every ~2s: ON 60ms, OFF 1940ms
      if (g_step == 0) { writeLed(true);  g_tNext = now + 60;  g_step = 1; }
      else             { writeLed(false); g_tNext = now + 1940; g_step = 0; }
      break;

    case LedMode::AP_DOUBLE:
      // ON 120, OFF 120, ON 120, OFF 1200 (repeat)
      if (g_step == 0) { writeLed(true);  g_tNext = now + 120; g_step = 1; }
      else if (g_step == 1) { writeLed(false); g_tNext = now + 120; g_step = 2; }
      else if (g_step == 2) { writeLed(true);  g_tNext = now + 120; g_step = 3; }
      else { writeLed(false); g_tNext = now + 1200; g_step = 0; }
      break;

    case LedMode::ERROR_TRIPLE:
      // ON 100, OFF 100 x3, then OFF 900
      // steps: 0 on,1 off,2 on,3 off,4 on,5 off,6 pause
      if (g_step == 0) { writeLed(true);  g_tNext = now + 100; g_step = 1; }
      else if (g_step == 1) { writeLed(false); g_tNext = now + 100; g_step = 2; }
      else if (g_step == 2) { writeLed(true);  g_tNext = now + 100; g_step = 3; }
      else if (g_step == 3) { writeLed(false); g_tNext = now + 100; g_step = 4; }
      else if (g_step == 4) { writeLed(true);  g_tNext = now + 100; g_step = 5; }
      else if (g_step == 5) { writeLed(false); g_tNext = now + 900; g_step = 0; }
      break;

    default:
      break;
  }
}
