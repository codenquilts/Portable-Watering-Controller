#include "mqtt_mgr.h"

#define MQTT_MAX_PACKET_SIZE 512

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <time.h>

#include "wifi_mgr.h" // wifiGetState(), wifiIpString()
#include "time_mgr.h"
#include "storage.h"
#include "config.h"

// -------------------- Broker --------------------
// MQTT broker settings are configured in src/config.h

// -------------------- Timing --------------------
static constexpr uint32_t PUB_SLOW_MS = 60UL * 1000UL; // general state
static constexpr uint32_t PUB_FAST_MS = 5UL * 1000UL;  // while pump running
static constexpr uint32_t RECON_MS = 5UL * 1000UL;

// -------------------- Globals --------------------
static WiFiClient g_net;
static PubSubClient g_mqtt(g_net);

static String g_devId;
static String g_base;     // "pwb/<devId>"
static String g_lwtTopic; // ".../state/online"
static String g_mqttHost;
static uint16_t g_mqttPort = 1883;

static uint32_t g_lastConnAttempt = 0;
static uint32_t g_lastSlowPub = 0;
static uint32_t g_lastFastPub = 0;

static bool g_prevPumpOn = false;

// These pointers are only set during mqttTick() so callback can act on commands
static RuntimeState *g_cbSt = nullptr;
static DeviceCfg *g_cbCfg = nullptr;

// -------------------- Helpers --------------------
// -------------------- Helpers --------------------
static void pubEvent(const char* type, const char* msg = nullptr, const DeviceCfg* cfg = nullptr);
static void pubStr(const String& t, const char* s, bool retain = true);
static void pubStr(const String& t, const String& s, bool retain = true);

static String topic(const char* suffix) { // suffix e.g. "state/voltage_v"
  return g_base + "/" + suffix;
}

static String haTopic(const char* component, const char* objectId) {
  return String("homeassistant/") + component + "/" + g_devId + "/" + objectId + "/config";
}

static String jsonEscape(const String& in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    const char c = in[i];
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  return out;
}

static String haJsonKey(const char* key) {
  String out;
  out.reserve(strlen(key) + 3);
  out += '"';
  out += key;
  out += '"';
  out += ':';
  return out;
}

static String haDeviceJson() {
  String deviceName = jsonEscape(g_devId);
  String displayName = jsonEscape(APP_NAME) + String(" ") + jsonEscape(g_devId);
  String swVersion = jsonEscape(FW_VERSION);

  String out;
  out.reserve(96);

  out += '"'; out += "dev"; out += '"'; out += ':';
  out += '{';

  out += '"'; out += "ids"; out += '"'; out += ':';
  out += '[';
  out += '"'; out += deviceName; out += '"';
  out += ']';
  out += ',';

  out += '"'; out += "name"; out += '"'; out += ':';
  out += '"'; out += displayName; out += '"';
  out += ',';

  out += '"'; out += "mdl"; out += '"'; out += ':';
  out += '"'; out += "ESP32 Portable Watering"; out += '"';
  out += ',';

  out += '"'; out += "mf"; out += '"'; out += ':';
  out += '"'; out += "OpenAI"; out += '"';
  out += ',';

  out += '"'; out += "sw"; out += '"'; out += ':';
  out += '"'; out += swVersion; out += '"';
  out += '}';

  return out;
}

static String haJsonStr(const char* key, const String& value) {
  String out = haJsonKey(key);
  out += '"';
  out += jsonEscape(value);
  out += '"';
  return out;
}

static String haJsonStr(const char* key, const char* value) {
  return haJsonStr(key, String(value));
}

static String haJsonNum(const char* key, int value) {
  return haJsonKey(key) + String(value);
}

static String haJsonNum(const char* key, float value) {
  return haJsonKey(key) + String(value, 2);
}

static String base64Encode(const String& in) {
  static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out;
  const uint8_t* data = (const uint8_t*)in.c_str();
  int len = in.length();
  for (int i = 0; i < len; i += 3) {
    uint32_t value = data[i] << 16;
    int count = 1;
    if (i + 1 < len) {
      value |= data[i + 1] << 8;
      ++count;
    }
    if (i + 2 < len) {
      value |= data[i + 2];
      ++count;
    }

    out += table[(value >> 18) & 0x3F];
    out += table[(value >> 12) & 0x3F];
    out += (count > 1 ? table[(value >> 6) & 0x3F] : '=');
    out += (count > 2 ? table[value & 0x3F] : '=');
  }
  return out;
}

static bool smtpReadLine(WiFiClient& client, String& line, unsigned long timeout = 5000) {
  line = "";
  unsigned long start = millis();
  while (millis() - start < timeout) {
    while (client.available()) {
      char c = client.read();
      if (c == '\r') continue;
      if (c == '\n') return true;
      line += c;
    }
    delay(1);
  }
  return false;
}

static bool smtpReadResponse(WiFiClient& client, int expectedCode, unsigned long timeout = 5000) {
  String line;
  unsigned long start = millis();
  while (millis() - start < timeout) {
    if (!smtpReadLine(client, line, timeout)) return false;
    if (line.length() < 3) continue;
    int code = line.substring(0, 3).toInt();
    if (code != expectedCode) {
      if (line.length() > 3 && line[3] == '-') continue;
      return false;
    }
    if (line.length() > 3 && line[3] == '-') continue;
    return true;
  }
  return false;
}

static bool smtpSendCommand(WiFiClient& client, const String& cmd, int expectedCode) {
  client.print(cmd);
  client.print("\r\n");
  return smtpReadResponse(client, expectedCode);
}

static bool smtpSendEmail(const DeviceCfg& cfg, const String& to, const String& subject, const String& body) {
  if (to.isEmpty() || cfg.smtpHost.isEmpty()) return false;

  static WiFiClientSecure secureClient;
  static WiFiClient plainClient;
  WiFiClient* client;

  if (cfg.smtpUseSsl) {
    secureClient.setInsecure();
    client = &secureClient;
  } else {
    client = &plainClient;
  }

  if (!client->connect(cfg.smtpHost.c_str(), cfg.smtpPort)) return false;
  if (!smtpReadResponse(*client, 220)) {
    client->stop();
    return false;
  }

  String hostname = g_devId;
  if (hostname.isEmpty()) hostname = APP_NAME;

  if (!smtpSendCommand(*client, String("EHLO ") + hostname, 250)) {
    client->stop();
    return false;
  }

  if (!cfg.smtpUser.isEmpty()) {
    if (!smtpSendCommand(*client, "AUTH LOGIN", 334)) {
      client->stop();
      return false;
    }
    if (!smtpSendCommand(*client, base64Encode(cfg.smtpUser), 334)) {
      client->stop();
      return false;
    }
    if (!smtpSendCommand(*client, base64Encode(cfg.smtpPass), 235)) {
      client->stop();
      return false;
    }
  }

  const String from = cfg.smtpFrom.isEmpty() ? String("pwb@example.com") : cfg.smtpFrom;

  if (!smtpSendCommand(*client, String("MAIL FROM:<") + from + ">", 250)) {
    client->stop();
    return false;
  }
  if (!smtpSendCommand(*client, String("RCPT TO:<") + to + ">", 250)) {
    client->stop();
    return false;
  }
  if (!smtpSendCommand(*client, "DATA", 354)) {
    client->stop();
    return false;
  }

  client->print("From: ");
  client->print(from);
  client->print("\r\n");
  client->print("To: ");
  client->print(to);
  client->print("\r\n");
  client->print("Subject: ");
  client->print(subject);
  client->print("\r\n");
  client->print("Content-Type: text/plain; charset=utf-8\r\n");
  client->print("\r\n");
  client->print(body);
  client->print("\r\n.\r\n");

  if (!smtpReadResponse(*client, 250)) {
    client->stop();
    return false;
  }

  smtpSendCommand(*client, "QUIT", 221);
  client->stop();
  return true;
}

static bool shouldEmailError(const char* type) {
  if (!type) return false;
  String t(type);
  return t.endsWith("_invalid") || t.endsWith("_refused");
}

static bool sendEmailNotification(const DeviceCfg& cfg, const String& subject, const String& body) {
  if (cfg.notifyEmail.isEmpty()) return false;
  return smtpSendEmail(cfg, cfg.notifyEmail, subject, body);
}

bool mqttSendTestEmail(const DeviceCfg& cfg) {
  if (cfg.notifyEmail.isEmpty() || cfg.smtpHost.isEmpty()) return false;

  String subject = String(APP_NAME) + " test email";
  String body = "Device: " + cfg.deviceName + "\n";
  body += "This is a test email from ";
  body += APP_NAME;
  body += ".\n";
  body += "Time: ";
  body += timeNowStringHM();
  body += "\n";

  return smtpSendEmail(cfg, cfg.notifyEmail, subject, body);
}

static void notifyErrorEmail(const DeviceCfg& cfg, const char* type, const char* msg) {
  if (!cfg.notifyErrors || cfg.notifyEmail.isEmpty() || !shouldEmailError(type)) return;

  String subject = String(APP_NAME) + " alert: " + type;
  String body = "Device: " + cfg.deviceName + "\n";
  body += "Type: ";
  body += type;
  body += "\n";
  if (msg && *msg) {
    body += "Details: ";
    body += msg;
    body += "\n";
  }
  body += "Time: ";
  body += timeNowStringHM();
  body += "\n";

  sendEmailNotification(cfg, subject, body);
}

static void pubDiscovery(const char* component, const char* objectId, const String& body) {
  pubStr(haTopic(component, objectId), body, true);
}

static void publishHomeAssistantDiscovery() {
  const String availability = haJsonStr("avty_t", g_lwtTopic) + "," +
                              haJsonStr("pl_avail", "1") + "," +
                              haJsonStr("pl_not_avail", "0");
  const String dev = haDeviceJson();
  const String stateBase = g_base + "/state/";
  const String cmdBase = g_base + "/cmd/";

  pubDiscovery(
      "switch",
      "pump",
      "{" +
      haJsonStr("name", "Pump") + "," +
      haJsonStr("uniq_id", g_devId + "_pump") + "," +
      haJsonStr("stat_t", stateBase + "pump_on") + "," +
      haJsonStr("cmd_t", cmdBase + "pump") + "," +
      haJsonStr("pl_on", "ON") + "," +
      haJsonStr("pl_off", "OFF") + "," +
      haJsonStr("stat_on", "1") + "," +
      haJsonStr("stat_off", "0") + "," +
      haJsonStr("icon", "mdi:water-pump") + "," +
      availability + "," + dev +
      "}");

  pubDiscovery(
      "button",
      "manual_run_morning",
      "{" +
      haJsonStr("name", "Run Morning") + "," +
      haJsonStr("uniq_id", g_devId + "_run_morning") + "," +
      haJsonStr("cmd_t", cmdBase + "run_now") + "," +
      haJsonStr("payload_press", "MORNING") + "," +
      haJsonStr("icon", "mdi:weather-sunset-up") + "," +
      availability + "," + dev +
      "}");

  pubDiscovery(
      "button",
      "manual_run_evening",
      "{" +
      haJsonStr("name", "Run Evening") + "," +
      haJsonStr("uniq_id", g_devId + "_run_evening") + "," +
      haJsonStr("cmd_t", cmdBase + "run_now") + "," +
      haJsonStr("payload_press", "EVENING") + "," +
      haJsonStr("icon", "mdi:weather-sunset-down") + "," +
      availability + "," + dev +
      "}");

  pubDiscovery(
      "button",
      "pump_stop",
      "{" +
      haJsonStr("name", "Stop Pump") + "," +
      haJsonStr("uniq_id", g_devId + "_pump_stop") + "," +
      haJsonStr("cmd_t", cmdBase + "stop") + "," +
      haJsonStr("payload_press", "STOP") + "," +
      haJsonStr("icon", "mdi:stop-circle") + "," +
      haJsonStr("entity_category", "config") + "," +
      availability + "," + dev +
      "}");

  pubDiscovery(
      "button",
      "reboot",
      "{" +
      haJsonStr("name", "Reboot") + "," +
      haJsonStr("uniq_id", g_devId + "_reboot") + "," +
      haJsonStr("cmd_t", cmdBase + "reboot") + "," +
      haJsonStr("payload_press", "1") + "," +
      haJsonStr("icon", "mdi:restart") + "," +
      haJsonStr("entity_category", "config") + "," +
      availability + "," + dev +
      "}");

  pubDiscovery(
      "number",
      "manual_run_minutes",
      "{" +
      haJsonStr("name", "Manual Run Minutes") + "," +
      haJsonStr("uniq_id", g_devId + "_manual_run_minutes") + "," +
      haJsonStr("cmd_t", cmdBase + "manual_run") + "," +
      haJsonNum("min", 1) + "," +
      haJsonNum("max", 15) + "," +
      haJsonNum("step", 1) + "," +
      haJsonStr("mode", "box") + "," +
      haJsonStr("optimistic", "true") + "," +
      haJsonStr("icon", "mdi:timer-outline") + "," +
      availability + "," + dev +
      "}");

  pubDiscovery(
      "number",
      "return_flow_ml_per_sec",
      "{" +
      haJsonStr("name", "Return Flow Rate") + "," +
      haJsonStr("uniq_id", g_devId + "_return_flow_ml_per_sec") + "," +
      haJsonStr("stat_t", stateBase + "return_flow_ml_per_sec") + "," +
      haJsonStr("cmd_t", cmdBase + "return_flow_ml_per_sec") + "," +
      haJsonStr("unit_of_measurement", "mL/s") + "," +
      haJsonNum("min", 0) + "," +
      haJsonNum("max", 100) + "," +
      haJsonNum("step", 0.1f) + "," +
      haJsonStr("mode", "box") + "," +
      haJsonStr("icon", "mdi:pipe-valve") + "," +
      haJsonStr("entity_category", "config") + "," +
      availability + "," + dev +
      "}");

  pubDiscovery(
      "sensor",
      "actual_flow_ml_per_sec",
      "{" +
      haJsonStr("name", "Actual Flow Rate") + "," +
      haJsonStr("uniq_id", g_devId + "_actual_flow_ml_per_sec") + "," +
      haJsonStr("stat_t", stateBase + "actual_flow_ml_per_sec") + "," +
      haJsonStr("unit_of_measurement", "mL/s") + "," +
      haJsonStr("state_class", "measurement") + "," +
      haJsonStr("icon", "mdi:water-pump") + "," +
      availability + "," + dev +
      "}");

  pubDiscovery(
      "sensor",
      "tank_ml",
      "{" +
      haJsonStr("name", "Tank Level") + "," +
      haJsonStr("uniq_id", g_devId + "_tank_ml") + "," +
      haJsonStr("stat_t", stateBase + "tank_ml") + "," +
      haJsonStr("unit_of_measurement", "mL") + "," +
      haJsonStr("device_class", "volume_storage") + "," +
      haJsonStr("state_class", "measurement") + "," +
      haJsonStr("icon", "mdi:water") + "," +
      availability + "," + dev +
      "}");

  pubDiscovery(
      "sensor",
      "voltage_v",
      "{" +
      haJsonStr("name", "Battery Voltage") + "," +
      haJsonStr("uniq_id", g_devId + "_voltage_v") + "," +
      haJsonStr("stat_t", stateBase + "voltage_v") + "," +
      haJsonStr("unit_of_measurement", "V") + "," +
      haJsonStr("device_class", "voltage") + "," +
      haJsonStr("state_class", "measurement") + "," +
      haJsonStr("icon", "mdi:battery") + "," +
      availability + "," + dev +
      "}");

  pubDiscovery(
      "sensor",
      "last_reason",
      "{" +
      haJsonStr("name", "Last Watering Reason") + "," +
      haJsonStr("uniq_id", g_devId + "_last_reason") + "," +
      haJsonStr("stat_t", stateBase + "last_reason") + "," +
      haJsonStr("icon", "mdi:history") + "," +
      haJsonStr("entity_category", "diagnostic") + "," +
      availability + "," + dev +
      "}");

  pubDiscovery(
      "binary_sensor",
      "low_tank",
      "{" +
      haJsonStr("name", "Low Tank") + "," +
      haJsonStr("uniq_id", g_devId + "_low_tank") + "," +
      haJsonStr("stat_t", stateBase + "low_tank") + "," +
      haJsonStr("pl_on", "1") + "," +
      haJsonStr("pl_off", "0") + "," +
      haJsonStr("device_class", "problem") + "," +
      haJsonStr("icon", "mdi:water-alert") + "," +
      availability + "," + dev +
      "}");

  static const char* kSchedNames[2] = {"morning", "evening"};
  static const char* kSchedLabels[2] = {"Morning", "Evening"};
  static const char* kSchedIcons[2] = {"mdi:weather-sunset-up", "mdi:weather-sunset-down"};
  static const char* kDayKeys[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};
  static const char* kDayLabels[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

  for (uint8_t i = 0; i < 2; ++i) {
    const String sched = kSchedNames[i];
    const String label = kSchedLabels[i];
    const String icon = kSchedIcons[i];
    const String schedStateBase = stateBase + "schedule/" + sched + "/";
    const String schedCmdBase = cmdBase + "schedule/" + sched + "/";

    pubDiscovery(
        "switch",
        (sched + "_enabled").c_str(),
        "{"
        "\"name\":\"" + label + " Schedule\","
        "\"uniq_id\":\"" + g_devId + "_" + sched + "_enabled\","
        "\"stat_t\":\"" + schedStateBase + "enabled\","
        "\"cmd_t\":\"" + schedCmdBase + "enabled\","
        "\"pl_on\":\"1\","
        "\"pl_off\":\"0\","
        "\"stat_on\":\"1\","
        "\"stat_off\":\"0\","
        "\"icon\":\"" + icon + "\","
        + availability + "," + dev +
        "}");

    pubDiscovery(
        "number",
        (sched + "_run_min").c_str(),
        "{"
        "\"name\":\"" + label + " Run Minutes\","
        "\"uniq_id\":\"" + g_devId + "_" + sched + "_run_min\","
        "\"stat_t\":\"" + schedStateBase + "run_min\","
        "\"cmd_t\":\"" + schedCmdBase + "run_min\","
        "\"min\":1,"
        "\"max\":15,"
        "\"step\":1,"
        "\"mode\":\"box\","
        "\"icon\":\"mdi:timer-outline\","
        + availability + "," + dev +
        "}");

    pubDiscovery(
        "number",
        (sched + "_days_mask").c_str(),
        "{"
        "\"name\":\"" + label + " Days Mask\","
        "\"uniq_id\":\"" + g_devId + "_" + sched + "_days_mask\","
        "\"stat_t\":\"" + schedStateBase + "days_mask\","
        "\"cmd_t\":\"" + schedCmdBase + "days_mask\","
        "\"min\":0,"
        "\"max\":127,"
        "\"step\":1,"
        "\"mode\":\"box\","
        "\"icon\":\"mdi:calendar-week\","
        "\"entity_category\":\"config\","
        + availability + "," + dev +
        "}");

    pubDiscovery(
        "text",
        (sched + "_start_hhmm").c_str(),
        "{"
        "\"name\":\"" + label + " Start HHMM\","
        "\"uniq_id\":\"" + g_devId + "_" + sched + "_start_hhmm\","
        "\"stat_t\":\"" + schedStateBase + "start_hhmm\","
        "\"cmd_t\":\"" + schedCmdBase + "start_hhmm\","
        "\"pattern\":\"^([01]?[0-9]|2[0-3])[0-5][0-9]$\","
        "\"mode\":\"text\","
        "\"icon\":\"mdi:clock-outline\","
        "\"entity_category\":\"config\","
        + availability + "," + dev +
        "}");

    pubDiscovery(
        "sensor",
        (sched + "_days_text").c_str(),
        "{"
        "\"name\":\"" + label + " Days\","
        "\"uniq_id\":\"" + g_devId + "_" + sched + "_days_text\","
        "\"stat_t\":\"" + schedStateBase + "days_text\","
        "\"icon\":\"mdi:calendar-text\","
        "\"entity_category\":\"diagnostic\","
        + availability + "," + dev +
        "}");

    for (uint8_t d = 0; d < 7; ++d) {
      pubDiscovery(
          "switch",
          (sched + "_day_" + kDayKeys[d]).c_str(),
          "{"
          "\"name\":\"" + label + " " + kDayLabels[d] + "\","
          "\"uniq_id\":\"" + g_devId + "_" + sched + "_day_" + kDayKeys[d] + "\","
          "\"stat_t\":\"" + schedStateBase + "day/" + kDayKeys[d] + "\","
          "\"cmd_t\":\"" + schedCmdBase + "day/" + kDayKeys[d] + "\","
          "\"pl_on\":\"1\","
          "\"pl_off\":\"0\","
          "\"stat_on\":\"1\","
          "\"stat_off\":\"0\","
          "\"icon\":\"mdi:calendar-week\","
          "\"entity_category\":\"config\","
          + availability + "," + dev +
          "}");
    }
  }
}

// Publish string payload (safe, minimal heap churn)
static void pubStr(const String& t, const char* s, bool retain) {
  if (!g_mqtt.connected()) return;
  if (!s) s = "";
  g_mqtt.publish(t.c_str(), s, retain);
}

static String sanitizeId(const String &s) {
  String out = s;
  out.toLowerCase();
  out.trim();
  if (out.isEmpty()) out = "watering";
  for (size_t i = 0; i < out.length(); i++) {
    char c = out[i];
    bool ok = (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              (c == '-');
    if (!ok) out.setCharAt(i, '-');
  }
  return out;
}

static int8_t dayKeyToIndex(const String& key) {
  if (key == "sun") return 0;
  if (key == "mon") return 1;
  if (key == "tue") return 2;
  if (key == "wed") return 3;
  if (key == "thu") return 4;
  if (key == "fri") return 5;
  if (key == "sat") return 6;
  return -1;
}

// Convenience overload kept (retains your existing call sites)
static void pubStr(const String& t, const String& s, bool retain) {
  pubStr(t, s.c_str(), retain);
}

static void pubInt(const String& t, int32_t v, bool retain = true) {
  if (!g_mqtt.connected()) return;
  char buf[16];
  snprintf(buf, sizeof(buf), "%ld", (long)v);
  g_mqtt.publish(t.c_str(), buf, retain);
}

static void pubUInt(const String& t, uint32_t v, bool retain = true) {
  if (!g_mqtt.connected()) return;
  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)v);
  g_mqtt.publish(t.c_str(), buf, retain);
}

static void pubFloat(const String& t, float v, uint8_t dp = 2, bool retain = true) {
  if (!g_mqtt.connected()) return;
  char buf[24];
  dtostrf(v, 0, dp, buf);
  g_mqtt.publish(t.c_str(), buf, retain);
}

// Keep epochNow() for any legacy internal use,
// but prefer timeNow() / timeIsValid() elsewhere.
static uint32_t epochNow() {
  time_t now = time(nullptr);
  if (now < 100000) return 0; // not set
  return (uint32_t)now;
}

static uint32_t pumpElapsedS(const RuntimeState& st) {
  if (!st.pumpOn) return 0;
  if (st.pumpStartMs == 0) return 0;
  return (millis() - st.pumpStartMs) / 1000UL;
}

static void formatDaysMask(uint8_t mask, char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  snprintf(out, outSize, "%u", (unsigned)(mask & 0x7F));
}

static void formatDaysText(uint8_t mask, char* out, size_t outSize) {
  static const char* kDayNames[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

  if (!out || outSize == 0) return;

  out[0] = 0;
  mask &= 0x7F;

  if (mask == 0) {
    snprintf(out, outSize, "none");
    return;
  }

  bool first = true;
  size_t used = 0;
  for (uint8_t i = 0; i < 7; ++i) {
    if ((mask & (1U << i)) == 0) continue;

    int written = snprintf(out + used, outSize - used, "%s%s", first ? "" : ",", kDayNames[i]);
    if (written < 0 || (size_t)written >= (outSize - used)) break;
    used += (size_t)written;
    first = false;
  }
}

static void publishScheduleDays(const char* prefix, uint8_t mask) {
  static const char* kDayKeys[7] = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};

  if (!prefix) return;

  mask &= 0x7F;

  char buf[32];
  formatDaysText(mask, buf, sizeof(buf));
  pubStr(topic((String(prefix) + "/days_text").c_str()), buf, true);

  for (uint8_t i = 0; i < 7; ++i) {
    pubInt(topic((String(prefix) + "/day/" + kDayKeys[i]).c_str()),
           (mask & (1U << i)) ? 1 : 0,
           true);
  }
}

static bool parseBoolPayload(const String& payload, bool& out) {
  String p = payload;
  p.trim();
  p.toUpperCase();

  if (p == "1" || p == "ON" || p == "TRUE" || p == "YES" || p == "ENABLE" || p == "ENABLED") {
    out = true;
    return true;
  }
  if (p == "0" || p == "OFF" || p == "FALSE" || p == "NO" || p == "DISABLE" || p == "DISABLED") {
    out = false;
    return true;
  }
  return false;
}

static bool parseUInt16Payload(const String& payload, uint16_t& out) {
  String p = payload;
  p.trim();
  if (!p.length()) return false;

  char* end = nullptr;
  unsigned long value = strtoul(p.c_str(), &end, 10);
  if (!end || *end != 0 || value > 65535UL) return false;
  out = (uint16_t)value;
  return true;
}

static bool parseFloatPayload(const String& payload, float& out) {
  String p = payload;
  p.trim();
  if (!p.length()) return false;

  char* end = nullptr;
  float value = strtof(p.c_str(), &end);
  if (!end || *end != 0) return false;
  out = value;
  return true;
}

static void publishScheduleState(const char* name, const ScheduleCfg& sched) {
  if (!name) return;

  const String base = topic((String("state/schedule/") + name).c_str());
  char hh[32];

  pubInt(base + "/enabled", sched.enabled ? 1 : 0, true);
  snprintf(hh, sizeof(hh), "%04u", (unsigned)sched.startHHMM);
  pubStr(base + "/start_hhmm", hh, true);
  pubInt(base + "/run_min", sched.runMin, true);
  formatDaysMask(sched.daysMask, hh, sizeof(hh));
  pubStr(base + "/days_mask", hh, true);
  publishScheduleDays((String("state/schedule/") + name).c_str(), sched.daysMask);
}

static bool updateScheduleField(const char* name, ScheduleCfg& sched, const String& field, const String& payload, const DeviceCfg& cfg) {
  if (field == "enabled") {
    bool enabled = sched.enabled;
    if (!parseBoolPayload(payload, enabled)) {
      pubEvent("cmd_schedule_invalid", name, &cfg);
      return false;
    }
    sched.enabled = enabled;
    return true;
  }

  if (field == "start_hhmm") {
    uint16_t start = sched.startHHMM;
    if (!parseUInt16Payload(payload, start) || start > 2359) {
      pubEvent("cmd_schedule_invalid", name, &cfg);
      return false;
    }
    sched.startHHMM = start;
    return true;
  }

  if (field == "run_min") {
    uint16_t run = sched.runMin;
    if (!parseUInt16Payload(payload, run)) {
      pubEvent("cmd_schedule_invalid", name, &cfg);
      return false;
    }
    if (run < RUN_MIN_MINUTES) run = RUN_MIN_MINUTES;
    if (run > RUN_MAX_MINUTES) run = RUN_MAX_MINUTES;
    sched.runMin = (uint8_t)run;
    return true;
  }

  if (field == "days_mask") {
    uint16_t mask = sched.daysMask;
    if (!parseUInt16Payload(payload, mask) || mask > 0x7F) {
      pubEvent("cmd_schedule_invalid", name, &cfg);
      return false;
    }
    sched.daysMask = (uint8_t)mask;
    return true;
  }

  return false;
}

static void applyMqttConfig(const DeviceCfg& cfg) {
  g_mqttHost = cfg.mqttHost;
  g_mqttHost.trim();
  g_mqttPort = cfg.mqttPort ? cfg.mqttPort : 1883;
  g_mqtt.setServer(g_mqttHost.c_str(), g_mqttPort);
}

static void applyMqttIdentity(const DeviceCfg& cfg) {
  g_devId = sanitizeId(cfg.deviceName);
  g_base = "watering/" + g_devId;
  g_lwtTopic = topic("state/online");
}

static bool handleScheduleCmd(const String& tpc, const String& payload, DeviceCfg& cfg) {
  static const char* kNames[2] = {"morning", "evening"};
  ScheduleCfg* schedules[2] = {&cfg.morning, &cfg.evening};

  for (uint8_t i = 0; i < 2; ++i) {
    const String prefix = topic((String("cmd/schedule/") + kNames[i] + "/").c_str());
    if (!tpc.startsWith(prefix)) continue;

    const String field = tpc.substring(prefix.length());
    if (!field.length()) return false;

    if (field.startsWith("day/")) {
      const int8_t dayIndex = dayKeyToIndex(field.substring(4));
      bool enabled = false;
      if (dayIndex < 0 || !parseBoolPayload(payload, enabled)) {
        pubEvent("cmd_schedule_invalid", kNames[i], &cfg);
        return true;
      }

      if (enabled) {
        schedules[i]->daysMask |= (uint8_t)(1U << dayIndex);
      } else {
        schedules[i]->daysMask &= (uint8_t)~(1U << dayIndex);
      }

      saveConfig(cfg);
      publishScheduleState(kNames[i], *schedules[i]);
      pubEvent("cmd_schedule_updated", kNames[i]);
      return true;
    }

    if (!updateScheduleField(kNames[i], *schedules[i], field, payload, cfg)) return true;

    saveConfig(cfg);
    publishScheduleState(kNames[i], *schedules[i]);
    pubEvent("cmd_schedule_updated", kNames[i]);
    return true;
  }

  return false;
}

static void pubEvent(const char* type, const char* msg, const DeviceCfg* cfg) {
  // Single JSON event topic (NOT retained)
  // Fixed buffer to avoid heap fragmentation from String concatenation
  if (!g_mqtt.connected()) return;

  uint32_t ts = timeIsValid() ? (uint32_t)timeNow() : 0;

  char buf[192];
  if (msg && *msg) {
    // NOTE: keep msg simple (no quotes) or add escaping later if needed.
    snprintf(buf, sizeof(buf),
             "{\"ts\":%lu,\"type\":\"%s\",\"msg\":\"%s\"}",
             (unsigned long)ts, type, msg);
  } else {
    snprintf(buf, sizeof(buf),
             "{\"ts\":%lu,\"type\":\"%s\"}",
             (unsigned long)ts, type);
  }

  g_mqtt.publish(topic("event").c_str(), buf, false);

  if (cfg && cfg->notifyErrors && shouldEmailError(type)) {
    notifyErrorEmail(*cfg, type, msg);
  }
}
// -------------------- Command handling --------------------
static void handleCmd(const String &tpc, const String &payload, RuntimeState &st, DeviceCfg &cfg)
{
  // Commands:
  // pwb/<id>/cmd/pump      -> ON / OFF
  // pwb/<id>/cmd/run_now   -> MORNING / EVENING
  // pwb/<id>/cmd/manual_run -> minutes
  // pwb/<id>/cmd/stop      -> 1 / STOP / OFF
  // pwb/<id>/cmd/reboot    -> 1
  // pwb/<id>/cmd/schedule/<morning|evening>/<field> -> payload

  if (handleScheduleCmd(tpc, payload, cfg))
  {
    return;
  }

  if (tpc.endsWith("/cmd/pump"))
  {
    String p = payload;
    p.trim();
    p.toUpperCase();

    if (p == "ON")
    {
      // Safety: refuse if low tank
      if (cfg.tankLevelMl <= cfg.minLevelMl)
      {
        pubEvent("cmd_refused_low_tank", "pump ON refused", &cfg);
        return;
      }
      // Default ON = morning run length
      pumpStartForMinutes(st, (uint8_t)cfg.morning.runMin, "MQTT_PUMP_ON");
      pubEvent("cmd_pump_on");
      return;
    }

    if (p == "OFF")
    {
      pumpStopWithReason(st, "MQTT_PUMP_OFF");
      pubEvent("cmd_pump_off");
      return;
    }
  }

  if (tpc.endsWith("/cmd/run_now"))
  {
    String p = payload;
    p.trim();
    p.toUpperCase();

    if (cfg.tankLevelMl <= cfg.minLevelMl)
    {
      pubEvent("cmd_refused_low_tank", "run_now refused", &cfg);
      return;
    }

    if (p == "MORNING")
    {
      pumpStartForMinutes(st, cfg.morning.runMin, "MQTT_RUN_MORNING");
      pubEvent("cmd_run_now", "morning");
      return;
    }

    if (p == "EVENING")
    {
      pumpStartForMinutes(st, cfg.evening.runMin, "MQTT_RUN_EVENING");
      pubEvent("cmd_run_now", "evening");
      return;
    }
  }

  if (tpc.endsWith("/cmd/manual_run"))
  {
    uint16_t run = 0;
    if (!parseUInt16Payload(payload, run))
    {
      pubEvent("cmd_manual_run_invalid", "bad_minutes", &cfg);
      return;
    }

    if (cfg.tankLevelMl <= cfg.minLevelMl)
    {
      pubEvent("cmd_refused_low_tank", "manual_run refused", &cfg);
      return;
    }

    if (run < RUN_MIN_MINUTES) run = RUN_MIN_MINUTES;
    if (run > RUN_MAX_MINUTES) run = RUN_MAX_MINUTES;

    pumpStartForMinutes(st, (uint8_t)run, "MQTT_MANUAL");

    char buf[16];
    snprintf(buf, sizeof(buf), "%u", (unsigned)run);
    pubEvent("cmd_manual_run", buf);
    return;
  }

  if (tpc.endsWith("/cmd/return_flow_ml_per_sec"))
  {
    float returnFlow = cfg.returnFlowMlPerSec;
    if (!parseFloatPayload(payload, returnFlow))
    {
      pubEvent("cmd_return_flow_invalid", "bad_flow", &cfg);
      return;
    }

    if (returnFlow < 0.0f) returnFlow = 0.0f;
    if (returnFlow > cfg.flowMlPerSec) returnFlow = cfg.flowMlPerSec;
    cfg.returnFlowMlPerSec = returnFlow;
    saveConfig(cfg);
    pubFloat(topic("state/return_flow_ml_per_sec"), cfg.returnFlowMlPerSec, 2, true);
    pubFloat(topic("state/actual_flow_ml_per_sec"), cfg.actualFlowMlPerSec(), 2, true);
    pubEvent("cmd_return_flow_updated");
    return;
  }

  if (tpc.endsWith("/cmd/stop"))
  {
    bool doStop = false;
    String p = payload;
    p.trim();
    p.toUpperCase();

    if (parseBoolPayload(payload, doStop) || p == "STOP" || p == "EMERGENCY")
    {
      if (doStop || p == "STOP" || p == "EMERGENCY")
      {
        pumpStopWithReason(st, "MQTT_STOP");
        pubEvent((p == "EMERGENCY") ? "cmd_emergency_stop" : "cmd_stop");
      }
      return;
    }

    pubEvent("cmd_stop_invalid", "bad_payload", &cfg);
    return;
  }

  if (tpc.endsWith("/cmd/reboot"))
  {
    String p = payload;
    p.trim();
    if (p == "1" || p.equalsIgnoreCase("true"))
    {
      pubEvent("cmd_reboot");
      delay(150);
      ESP.restart();
    }
    return;
  }
}

static void onMqttMessage(char *tpc, byte *payload, unsigned int length)
{
  String topicStr(tpc);
  String msg;
  msg.reserve(length + 1);
  for (unsigned int i = 0; i < length; i++)
    msg += (char)payload[i];

  if (g_cbSt && g_cbCfg)
  {
    handleCmd(topicStr, msg, *g_cbSt, *g_cbCfg);
  }
}

// -------------------- Connect --------------------
void mqttBegin(const DeviceCfg &cfg)
{
  applyMqttIdentity(cfg);
  applyMqttConfig(cfg);
  g_mqtt.setCallback(onMqttMessage);
  g_mqtt.setKeepAlive(30);
  g_mqtt.setSocketTimeout(2);
}

static bool mqttConnect(const DeviceCfg& cfg)
{
  if (wifiGetState() != WifiModeState::STA_CONNECTED)
    return false;
  if (g_mqttHost.isEmpty())
    return false;

  // Unique client ID per device
  uint64_t mac = ESP.getEfuseMac();
  char cid[40];
  snprintf(cid, sizeof(cid), "pwb-%s-%02X%02X%02X",
           g_devId.c_str(),
           (uint8_t)(mac >> 16), (uint8_t)(mac >> 8), (uint8_t)(mac >> 0));

  // LWT retained: online=0
  bool ok;
  if (!cfg.mqttUser.isEmpty())
  {
    ok = g_mqtt.connect(cid, cfg.mqttUser.c_str(), cfg.mqttPass.c_str(), g_lwtTopic.c_str(), 0, true, "0");
  }
  else
  {
    ok = g_mqtt.connect(cid, g_lwtTopic.c_str(), 0, true, "0");
  }
  if (!ok)
    return false;

  // Connected
  pubStr(g_lwtTopic, "1", true);
  pubStr(topic("state/ip"), wifiIpString(), true);
  pubStr(topic("state/wifi_mode"), "STA", true);
  publishHomeAssistantDiscovery();
  Serial.println("MQTT discovery config published");
  pubEvent("mqtt_connected");

  // Subscribe commands (QoS 1)
  g_mqtt.subscribe(topic("cmd/pump").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/run_now").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/manual_run").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/return_flow_ml_per_sec").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/stop").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/reboot").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/morning/enabled").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/morning/start_hhmm").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/morning/run_min").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/morning/days_mask").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/morning/day/sun").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/morning/day/mon").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/morning/day/tue").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/morning/day/wed").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/morning/day/thu").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/morning/day/fri").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/morning/day/sat").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/evening/enabled").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/evening/start_hhmm").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/evening/run_min").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/evening/days_mask").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/evening/day/sun").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/evening/day/mon").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/evening/day/tue").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/evening/day/wed").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/evening/day/thu").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/evening/day/fri").c_str(), 1);
  g_mqtt.subscribe(topic("cmd/schedule/evening/day/sat").c_str(), 1);

  return true;
}

bool mqttConnected()
{
  return g_mqtt.connected();
}

// -------------------- Publish --------------------
static void publishSlow(const DeviceCfg &cfg, RuntimeState &st, SensorsState &ss)
{
  pubStr(topic("state/wifi_mode"), (wifiGetState() == WifiModeState::STA_CONNECTED) ? "STA" : "AP", true);
  pubStr(topic("state/ip"), wifiIpString(), true);

  pubInt(topic("state/uptime_s"), (int32_t)(millis() / 1000UL), true);

  time_t now = timeNow();

  // ---- Time (soft clock + NTP upgrade) ----
  pubInt(topic("state/time_valid"), timeIsValid() ? 1 : 0, true);

  // Epoch (0 if invalid)
  pubUInt(topic("state/time_epoch"), timeIsValid() ? (uint32_t)timeNow() : 0, true);

  // Human time (or "--:--" if invalid)
  {
    String hhmm = timeNowStringHM();
    pubStr(topic("state/time_hhmm"), hhmm.c_str(), true);
  }

  // Source: "none" | "stored" | "ntp"
  pubStr(topic("state/time_src"), timeSourceStr(), true);

  pubInt(topic("state/boot_ready"), st.bootReady ? 1 : 0, true);

  // Sensors
  pubFloat(topic("state/voltage_v"), ss.voltageV, 2, true);

  // Tank/config
  pubFloat(topic("state/tank_ml"), cfg.tankLevelMl, 0, true);
  pubFloat(topic("state/tank_min_ml"), cfg.minLevelMl, 0, true);
  pubFloat(topic("state/tank_reset_ml"), cfg.resetLevelMl, 0, true);
  pubFloat(topic("state/usage_ml"), cfg.usageMl, 0, true);
  pubFloat(topic("state/flow_ml_per_sec"), cfg.flowMlPerSec, 2, true);
  pubFloat(topic("state/return_flow_ml_per_sec"), cfg.returnFlowMlPerSec, 2, true);
  pubFloat(topic("state/actual_flow_ml_per_sec"), cfg.actualFlowMlPerSec(), 2, true);
  bool lowTank = (cfg.tankLevelMl <= cfg.minLevelMl);
  pubInt(topic("state/low_tank"), lowTank ? 1 : 0, true);
  if (cfg.notifyLowTank && !cfg.notifyEmail.isEmpty()) {
    if (lowTank && !st.lowTankEmailSent) {
      String subject = String(APP_NAME) + " low tank warning";
      String body = "Device: " + cfg.deviceName + "\n";
      body += "Tank level: " + String((int)cfg.tankLevelMl) + " mL\n";
      body += "Minimum: " + String((int)cfg.minLevelMl) + " mL\n";
      body += "Time: " + timeNowStringHM() + "\n";
      if (sendEmailNotification(cfg, subject, body)) {
        st.lowTankEmailSent = true;
      }
    } else if (!lowTank) {
      st.lowTankEmailSent = false;
    }
  }

  // Schedules
  char hh[8];

  pubInt(topic("state/schedule/morning/enabled"), cfg.morning.enabled ? 1 : 0, true);
  snprintf(hh, sizeof(hh), "%04u", (unsigned)cfg.morning.startHHMM);
  pubStr(topic("state/schedule/morning/start_hhmm"), hh, true);
  pubInt(topic("state/schedule/morning/run_min"), cfg.morning.runMin, true);
  formatDaysMask(cfg.morning.daysMask, hh, sizeof(hh));
  pubStr(topic("state/schedule/morning/days_mask"), hh, true);
  publishScheduleDays("state/schedule/morning", cfg.morning.daysMask);

  pubInt(topic("state/schedule/evening/enabled"), cfg.evening.enabled ? 1 : 0, true);
  snprintf(hh, sizeof(hh), "%04u", (unsigned)cfg.evening.startHHMM);
  pubStr(topic("state/schedule/evening/start_hhmm"), hh, true);
  pubInt(topic("state/schedule/evening/run_min"), cfg.evening.runMin, true);
  formatDaysMask(cfg.evening.daysMask, hh, sizeof(hh));
  pubStr(topic("state/schedule/evening/days_mask"), hh, true);
  publishScheduleDays("state/schedule/evening", cfg.evening.daysMask);

  // Pump state + RAM log fields
  pubInt(topic("state/pump_on"), st.pumpOn ? 1 : 0, true);
  pubUInt(topic("state/pump_elapsed_s"), pumpElapsedS(st), true);
  pubUInt(topic("state/pump_remaining_s"), pumpRemainingSeconds(st), true);
  pubUInt(topic("state/pump_requested_s"), st.requestedRunS, true);

  pubUInt(topic("state/last_start_ts"), st.lastStartTs, true);
  pubUInt(topic("state/last_stop_ts"), st.lastStopTs, true);
  pubUInt(topic("state/last_run_s"), st.lastRunS, true);
  pubStr(topic("state/last_reason"), st.lastReason, true);
}

static void publishFast(const DeviceCfg &cfg, RuntimeState &st, SensorsState &ss)
{
  // While pump running, publish these more often
  pubInt(topic("state/pump_on"), st.pumpOn ? 1 : 0, true);

  // elapsed seconds since start
  pubUInt(topic("state/pump_elapsed_s"), pumpElapsedS(st), true);
  pubUInt(topic("state/pump_remaining_s"), pumpRemainingSeconds(st), true);
  pubUInt(topic("state/pump_requested_s"), st.requestedRunS, true);

  pubFloat(topic("state/tank_ml"), cfg.tankLevelMl, 0, true);
  pubFloat(topic("state/voltage_v"), ss.voltageV, 2, true);
}

// -------------------- Tick --------------------
void mqttTick(DeviceCfg &cfg, RuntimeState &st, SensorsState &ss)
{
  // Only use MQTT when STA is connected
  if (wifiGetState() != WifiModeState::STA_CONNECTED)
    return;

  const String configuredDevId = sanitizeId(cfg.deviceName);
  if (configuredDevId != g_devId) {
    if (g_mqtt.connected()) g_mqtt.disconnect();
    applyMqttIdentity(cfg);
  }

  String configuredHost = cfg.mqttHost;
  configuredHost.trim();
  const uint16_t configuredPort = cfg.mqttPort ? cfg.mqttPort : 1883;
  if (configuredHost != g_mqttHost || configuredPort != g_mqttPort) {
    if (g_mqtt.connected()) g_mqtt.disconnect();
    applyMqttConfig(cfg);
  }

  if (g_mqttHost.isEmpty()) {
    if (g_mqtt.connected()) g_mqtt.disconnect();
    return;
  }

  if (!g_mqtt.connected())
  {
    if (millis() - g_lastConnAttempt >= RECON_MS)
    {
      g_lastConnAttempt = millis();
      mqttConnect(cfg);
    }
    return;
  }

  // Allow callback to apply commands
  g_cbSt = &st;
  g_cbCfg = &cfg;

  g_mqtt.loop();

  // Event on pump change
  if (st.pumpOn != g_prevPumpOn)
  {
    g_prevPumpOn = st.pumpOn;
    pubEvent(st.pumpOn ? "pump_on" : "pump_off");
  }

  uint32_t now = millis();

  if (now - g_lastSlowPub >= PUB_SLOW_MS)
  {
    g_lastSlowPub = now;
    publishSlow(cfg, st, ss);
  }

  if (st.pumpOn && (now - g_lastFastPub >= PUB_FAST_MS))
  {
    g_lastFastPub = now;
    publishFast(cfg, st, ss);
  }

  g_cbSt = nullptr;
  g_cbCfg = nullptr;
}
