#include "tank.h"
#include "config.h"

// Divider: Battery+ -> R1(top) -> ADC -> R2(bottom) -> GND
// 68k / 20k => ratio = (R1+R2)/R2 = 88/20 = 4.40
static constexpr float DIVIDER_RATIO = 4.40f;

// Treat tiny ADC readings as "not connected" (floating input / unplugged divider)
static constexpr uint32_t VBAT_PRESENT_MV = 50;   // ADC pin millivolts threshold

static uint32_t lastTankTick = 0;
static uint32_t lastSaveMs   = 0;

float readVoltageV() {
  // Best on ESP32: calibrated reading
  uint32_t mv = analogReadMilliVolts(PIN_ADC_VBAT);   // millivolts at ADC pin

  // If the divider isn't connected, the pin can float.
  // Clamp to 0V to avoid misleading readings.
  if (mv < VBAT_PRESENT_MV) return 0.0f;

  float adcV  = mv / 1000.0f;              // volts at ADC pin
  float batV  = adcV * DIVIDER_RATIO;      // battery volts
  return batV;
}

void tankBegin() {
  // ESP32 recommended for higher input range
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_ADC_VBAT, ADC_11db);

  // Optional: discard a couple of reads after boot
  (void)analogReadMilliVolts(PIN_ADC_VBAT);
  delay(5);
  (void)analogReadMilliVolts(PIN_ADC_VBAT);
}

void tankLoop(DeviceCfg& cfg, RuntimeState& st, SensorsState& ss) {
  // update voltage often
  ss.voltageV = readVoltageV();

  // 5s tank tick
  if (millis() - lastTankTick < TANK_TICK_MS) return;
  lastTankTick = millis();

  if (st.pumpOn) {
    float dec = cfg.flowMlPerSec * (TANK_TICK_MS / 1000.0f);
    cfg.tankLevelMl -= dec;
    cfg.usageMl     += dec;

    if (cfg.tankLevelMl < cfg.minLevelMl) {
      cfg.tankLevelMl = cfg.minLevelMl;
      // caller enforces pump stop if cfg.tankLevelMl <= cfg.minLevelMl
    }
  }

  // persist occasionally (avoid flash wear): every ~60s
  if (millis() - lastSaveMs > 60000UL) {
    saveConfig(cfg);
    lastSaveMs = millis();
  }
}