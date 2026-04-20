#include "mqtt_mgr.h"

#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>

#include "wifi_mgr.h"   // wifiGetState(), wifiIpString()

// -------------------- Broker --------------------
#ifndef MQTT_HOST
#define MQTT_HOST "192.168.0.17"   // Synology / Mosquitto container host
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif

// No security (per your setup)
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASS
#define MQTT_PASS ""
#endif

// -------------------- Timing --------------------
static constexpr uint32_t PUB_SLOW_MS  = 60UL * 1000UL; // general state
static constexpr uint32_t PUB_FAST_MS  = 5UL  * 1000UL; // while pump running
static constexpr uint32_t RECON_MS     = 5UL  * 1000UL;

// -------------------- Globals --------------------
static WiFiClient   g_net;
static PubSubClient g_mqtt(g_net);

static String g_devId;
static String g_base;      // "pwb/<devId>"
static String g_lwtTopic;  // ".../state/online"

static uint32_t g_lastConnAttempt = 0;
static uint32_t g_lastSlowPub = 0;
static uint32_t g_lastFastPub = 0;

static bool g_prevPumpOn = false;

// These pointers are only set during mqttTick() so callback can act on commands
static RuntimeState*    g_cbSt  = nullptr;
static const DeviceCfg* g_cbCfg = nullptr;

// -------------------- Helpers --------------------
static String sanitizeId(const String& s) {
  String out = s;
  out.toLowerCase();
  out.trim();
  if (out.isEmpty()) out = "watering";
  for (size_t i = 0; i < out.length(); i++) {
    char c = out[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-');
    if (!ok) out.setCharAt(i, '-');
  }
  return out;
}

static String topic(const char* suffix) {  // suffix e.g. "state/voltage_v"
  return g_base + "/" + suffix;
}

static void pubStr(const String& t, const String& s, bool retain=true) {
  g_mqtt.publish(t.c_str(), s.c_str(), retain);
}

static void pubInt(const String& t, int32_t v, bool retain=true) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%ld", (long)v);
  g_mqtt.publish(t.c_str(), buf, retain);
}

static void pubUInt(const String& t, uint32_t v, bool retain=true) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)v);
  g_mqtt.publish(t.c_str(), buf, retain);
}

static void pubFloat(const String& t, float v, uint8_t dp=2, bool retain=true) {
  char buf[24];
  dtostrf(v, 0, dp, buf);
  g_mqtt.publish(t.c_str(), buf, retain);
}

static uint32_t epochNow() {
  time_t now = time(nullptr);
  if (now < 100000) return 0; // not set
  return (uint32_t)now;
}

static uint32_t pumpElapsedS(const RuntimeState& st) {
  // If time not valid or no start time, return 0
  uint32_t now = epochNow();
  if (!st.pumpOn) return 0;
  if (!st.lastStartTs) return 0;
  if (!now) return 0;
  if (now < st.lastStartTs) return 0;
  return now - st.lastStartTs;
}

static void pubEvent(const char* type, const char* msg=nullptr) {
  // Single JSON event topic (NOT retained)
  // Keep it small, no ArduinoJson dependency
  uint32_t ts = epochNow();
  String p = String("{\"ts\":") + ts + ",\"type\":\"" + type + "\"";
  if (msg && *msg) p += String(",\"msg\":\"") + msg + "\"";
  p += "}";
  g_mqtt.publish(topic("event").c_str(), p.c_str(), false);
}

// -------------------- Command handling --------------------
static void handleCmd(const String& tpc, const String& payload, RuntimeState& st, const DeviceCfg& cfg) {
  // Commands:
  // pwb/<id>/cmd/pump      -> ON / OFF
  // pwb/<id>/cmd/run_now   -> MORNING / EVENING
  // pwb/<id>/cmd/reboot    -> 1

  if (tpc.endsWith("/cmd/pump")) {
    String p = payload; p.trim(); p.toUpperCase();

    if (p == "ON") {
      // Safety: refuse if low tank
      if (cfg.tankLevelMl <= cfg.minLevelMl) {
        pubEvent("cmd_refused_low_tank", "pump ON refused");
        return;
      }
      // Default ON = morning run length
      pumpStartForMinutes(st, (uint8_t)cfg.morning.runMin, "MQTT_PUMP_ON");
      pubEvent("cmd_pump_on");
      return;
    }

    if (p == "OFF") {
      // Mark reason before stopping
      strncpy(st.lastReason, "MQTT_PUMP_OFF", sizeof(st.lastReason)-1);
      st.lastReason[sizeof(st.lastReason)-1] = 0;

      pumpStop(st);
      pubEvent("cmd_pump_off");
      return;
    }
  }

  if (tpc.endsWith("/cmd/run_now")) {
    String p = payload; p.trim(); p.toUpperCase();

    if (cfg.tankLevelMl <= cfg.minLevelMl) {
      pubEvent("cmd_refused_low_tank", "run_now refused");
      return;
    }

    if (p == "MORNING") {
      pumpStartForMinutes(st, cfg.morning.runMin, "MQTT_RUN_MORNING");
      pubEvent("cmd_run_now", "morning");
      return;
    }

    if (p == "EVENING") {
      pumpStartForMinutes(st, cfg.evening.runMin, "MQTT_RUN_EVENING");
      pubEvent("cmd_run_now", "evening");
      return;
    }
  }

  if (tpc.endsWith("/cmd/reboot")) {
    String p = payload; p.trim();
    if (p == "1" || p.equalsIgnoreCase("true")) {
      pubEvent("cmd_reboot");
      delay(150);
      ESP.restart();
    }
    return;
  }
}

static void onMqttMessage(char* tpc, byte* payload, unsigned int length) {
  String topicStr(tpc);
  String msg;
  msg.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  if (g_cbSt && g_cbCfg) {
    handleCmd(topicStr, msg, *g_cbSt, *g_cbCfg);
  }
}

// -------------------- Connect --------------------
void mqttBegin(const DeviceCfg& cfg) {
  g_devId = sanitizeId(cfg.deviceName);
  g_base  = "watering/" + g_devId;
  g_lwtTopic = topic("state/online");

  g_mqtt.setServer(MQTT_HOST, MQTT_PORT);
  g_mqtt.setCallback(onMqttMessage);
  g_mqtt.setKeepAlive(30);
  g_mqtt.setSocketTimeout(3);
}

static bool mqttConnect() {
  if (wifiGetState() != WifiModeState::STA_CONNECTED) return false;

  // Unique client ID per device
  uint64_t mac = ESP.getEfuseMac();
  char cid[40];
  snprintf(cid, sizeof(cid), "pwb-%s-%02X%02X%02X",
           g_devId.c_str(),
           (uint8_t)(mac >> 16), (uint8_t)(mac >> 8), (uint8_t)(mac >> 0));

  // LWT retained: online=0
  bool ok;
  if (strlen(MQTT_USER) > 0) {
    ok = g_mqtt.connect(cid, MQTT_USER, MQTT_PASS, g_lwtTopic.c_str(), 0, true, "0");
  } else {
    ok = g_mqtt.connect(cid, g_lwtTopic.c_str(), 0, true, "0");
  }
  if (!ok) return false;

  // Connected
  pubStr(g_lwtTopic, "1", true);
  pubStr(topic("state/ip"), wifiIpString(), true);
  pubStr(topic("state/wifi_mode"), "STA", true);
  pubEvent("mqtt_connected");

  // Subscribe commands (QoS 1)
  g_mqtt.subscribe(topic("cmd/pump").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/run_now").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/reboot").c_str(), 1);

  return true;
}

bool mqttConnected() {
  return g_mqtt.connected();
}

// -------------------- Publish --------------------
static void publishSlow(const DeviceCfg& cfg, RuntimeState& st, SensorsState& ss) {
  pubStr(topic("state/wifi_mode"), (wifiGetState()==WifiModeState::STA_CONNECTED) ? "STA" : "AP", true);
  pubStr(topic("state/ip"), wifiIpString(), true);

  pubInt(topic("state/uptime_s"), (int32_t)(millis()/1000UL), true);

  uint32_t ts = epochNow();
  pubInt(topic("state/time_valid"), (ts > 1700000000UL) ? 1 : 0, true); // ~2023+
  pubInt(topic("state/boot_ready"), st.bootReady ? 1 : 0, true);

  // Sensors
  pubFloat(topic("state/voltage_v"), ss.voltageV, 2, true);

  // Tank/config
  pubFloat(topic("state/tank_ml"), cfg.tankLevelMl, 0, true);
  pubFloat(topic("state/tank_min_ml"), cfg.minLevelMl, 0, true);
  pubFloat(topic("state/tank_reset_ml"), cfg.resetLevelMl, 0, true);
  pubFloat(topic("state/usage_ml"), cfg.usageMl, 0, true);
  pubInt(topic("state/low_tank"), (cfg.tankLevelMl <= cfg.minLevelMl) ? 1 : 0, true);

  // Schedules
  char hh[8];

  pubInt(topic("state/schedule/morning/enabled"), cfg.morning.enabled ? 1 : 0, true);
  snprintf(hh, sizeof(hh), "%04u", (unsigned)cfg.morning.startHHMM);
  pubStr(topic("state/schedule/morning/start_hhmm"), hh, true);
  pubInt(topic("state/schedule/morning/run_min"), cfg.morning.runMin, true);

  pubInt(topic("state/schedule/evening/enabled"), cfg.evening.enabled ? 1 : 0, true);
  snprintf(hh, sizeof(hh), "%04u", (unsigned)cfg.evening.startHHMM);
  pubStr(topic("state/schedule/evening/start_hhmm"), hh, true);
  pubInt(topic("state/schedule/evening/run_min"), cfg.evening.runMin, true);

  // Pump state + RAM log fields
  pubInt(topic("state/pump_on"), st.pumpOn ? 1 : 0, true);

  // Replace "remaining" with elapsed + last run info (works without knowing stop timer)
  pubUInt(topic("state/pump_elapsed_s"), pumpElapsedS(st), true);

  pubUInt(topic("state/last_start_ts"), st.lastStartTs, true);
  pubUInt(topic("state/last_stop_ts"),  st.lastStopTs,  true);
  pubUInt(topic("state/last_run_s"),    st.lastRunS,     true);
  pubStr(topic("state/last_reason"),    String(st.lastReason), true);
}

static void publishFast(const DeviceCfg& cfg, RuntimeState& st, SensorsState& ss) {
  // While pump running, publish these more often
  pubInt(topic("state/pump_on"), st.pumpOn ? 1 : 0, true);

  // elapsed seconds since start
  pubUInt(topic("state/pump_elapsed_s"), pumpElapsedS(st), true);

  pubFloat(topic("state/tank_ml"), cfg.tankLevelMl, 0, true);
  pubFloat(topic("state/voltage_v"), ss.voltageV, 2, true);
}

// -------------------- Tick --------------------
void mqttTick(const DeviceCfg& cfg, RuntimeState& st, SensorsState& ss) {
  // Only use MQTT when STA is connected
  if (wifiGetState() != WifiModeState::STA_CONNECTED) return;

  if (!g_mqtt.connected()) {
    if (millis() - g_lastConnAttempt >= RECON_MS) {
      g_lastConnAttempt = millis();
      mqttConnect();
    }
    return;
  }

  // Allow callback to apply commands
  g_cbSt = &st;
  g_cbCfg = &cfg;

  g_mqtt.loop();

  // Event on pump change
  if (st.pumpOn != g_prevPumpOn) {
    g_prevPumpOn = st.pumpOn;
    pubEvent(st.pumpOn ? "pump_on" : "pump_off");
  }

  uint32_t now = millis();

  if (now - g_lastSlowPub >= PUB_SLOW_MS) {
    g_lastSlowPub = now;
    publishSlow(cfg, st, ss);
  }

  if (st.pumpOn && (now - g_lastFastPub >= PUB_FAST_MS)) {
    g_lastFastPub = now;
    publishFast(cfg, st, ss);
  }

  g_cbSt = nullptr;
  g_cbCfg = nullptr;
}