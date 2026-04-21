#pragma once
#include <Arduino.h>
#include "storage.h"
#include "scheduler.h"
#include "tank.h"

void mqttBegin(const DeviceCfg& cfg);
void mqttTick(DeviceCfg& cfg, RuntimeState& st, SensorsState& ss);
bool mqttConnected();
bool mqttSendTestEmail(const DeviceCfg& cfg);
