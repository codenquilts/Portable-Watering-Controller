#pragma once
#include <Arduino.h>

enum class LedMode : uint8_t {
  OFF = 0,
  SOLID_ON,
  BOOT_FAST,
  HEARTBEAT,
  AP_DOUBLE,
  ERROR_TRIPLE
};

void ledBegin(uint8_t pin, bool activeLow = true);
void ledSetMode(LedMode mode);
LedMode ledGetMode();

void ledSetError(bool isError);   // optional helper
void ledTick();                   // call often from loop()
