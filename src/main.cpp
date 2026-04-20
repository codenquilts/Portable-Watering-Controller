#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "storage.h"
#include "wifi_mgr.h"
#include "scheduler.h"
#include "tank.h"
#include "web.h"
#include "led_status.h"
#include "mqtt_mgr.h"

static DeviceCfg     cfg;
static RuntimeState  st;
static SensorsState  ss;

static uint32_t bootStartMs = 0;

static bool timeValid() {
  struct tm t;
  if (!getLocalTime(&t, 50)) return false;
  return (t.tm_year + 1900) > 2023;
}

static void timeInit() {
  // TZ_INFO, NTP1, NTP2 expected in config.h
  configTzTime(TZ_INFO, NTP1, NTP2);
}

void setup() {
  Serial.begin(115200);
  delay(150);

  bootStartMs = millis();
  
  // Booting message
  Serial.printf("%s v%s booting... (%s)\n", APP_NAME, FW_VERSION, FW_BUILD);

  // LED early boot indication
  ledBegin(LED_PIN, LED_ACTIVE_LOW);
  ledSetMode(LedMode::BOOT_FAST);

  loadConfig(cfg);

  schedulerBegin();
  tankBegin();

  // WiFi (STA first, fallback AP)
  WifiModeState ws = wifiBegin(cfg);
  Serial.printf("WiFi connected? mode=%s ip=%s\n",
                (ws == WifiModeState::STA_CONNECTED) ? "STA" : "AP",
                wifiIpString().c_str());

  // Web UI + API ASAP (in BOTH AP and STA)
  webBegin(cfg, st, ss);

  // LED indicates network state (pump will override to SOLID_ON when running)
  ledSetMode(ws == WifiModeState::STA_CONNECTED ? LedMode::HEARTBEAT : LedMode::AP_DOUBLE);

 // added v1.3
  static bool s_timeInitDone = false;
if (!s_timeInitDone) {
  timeInit();
  s_timeInitDone = true;
}

  st.bootReady = false;

  Serial.printf("WEB READY. Mode=%s IP=%s\n",
                (ws == WifiModeState::STA_CONNECTED) ? "STA" : "AP",
                wifiIpString().c_str());

  mqttBegin(cfg);

}

void loop() {
  webLoop();
  ledTick();

  // Sensor/tank loop
  tankLoop(cfg, st, ss);

  // Low tank hard stop
  if (st.pumpOn && cfg.tankLevelMl <= cfg.minLevelMl) {
    pumpStop(st);
    saveConfig(cfg);
  }

  // Boot readiness gate: require time valid
  if (!st.bootReady) {
    if (timeValid()) {
      st.bootReady = true;
      resetDailyTriggers(cfg);
      saveConfig(cfg);
    }
  }

  // Scheduler (your schedulerLoop should internally skip if !st.bootReady if you want)
  schedulerLoop(cfg, st);

  mqttTick(cfg, st, ss);

  delay(5);
}
