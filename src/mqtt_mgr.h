#pragma once
#include <Arduino.h>
#include "storage.h"
#include "scheduler.h"
#include "tank.h"

void mqttBegin(const DeviceCfg& cfg);
void mqttTick(const DeviceCfg& cfg, RuntimeState& st, SensorsState& ss);
bool mqttConnected();