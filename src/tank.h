#pragma once
#include <Arduino.h>
#include "storage.h"
#include "scheduler.h"

struct SensorsState {
  float voltageV = 0.0f;
};

void tankBegin();
void tankLoop(DeviceCfg& cfg, RuntimeState& st, SensorsState& ss);
float readVoltageV();  // uses ADC + divider calibration
