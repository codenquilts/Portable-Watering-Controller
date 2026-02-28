#pragma once
#include "storage.h"

struct RuntimeState; // defined in scheduler.h
struct SensorsState; // defined in tank.h

void webBegin(DeviceCfg& cfg, RuntimeState& st, SensorsState& ss);
void webLoop();
