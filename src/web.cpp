// web.cpp — clean replacement (WiFiManager setup-mode, no /api/scan, no old /api/wifi save)

#include "web.h"

#include "scheduler.h" // RuntimeState + pump functions
#include "tank.h"      // SensorsState

#include "config.h"
#include "wifi_mgr.h"
#include "ui_index_html.h"
#include "storage.h"
#include "mqtt_mgr.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <time.h>
#include "time_mgr.h"

static AsyncWebServer server(80);
static DNSServer dns;

static DeviceCfg *g_cfg = nullptr;
static RuntimeState *g_st = nullptr;
static SensorsState *g_ss = nullptr;

// ------------------------------
// helpers
// ------------------------------
static uint8_t sanitizeDaysMask(uint8_t mask)
{
  return mask & 0x7F;
}

static void addScheduleJson(JsonObject obj, const ScheduleCfg &sched)
{
  obj["start_hhmm"] = sched.startHHMM;
  obj["run_min"] = sched.runMin;
  obj["enabled"] = sched.enabled;
  const uint8_t daysMask = sanitizeDaysMask(sched.daysMask);
  obj["days_mask"] = daysMask;

  JsonArray days = obj.createNestedArray("days");
  for (uint8_t i = 0; i < 7; ++i)
  {
    days.add((daysMask & (1U << i)) != 0);
  }
}

static String hhmmString()
{
  // Soft-clock string (works offline if stored epoch exists)
  return timeNowStringHM(); // returns "--:--" if not valid yet
}

static void sendJson(AsyncWebServerRequest *req, JsonDocument &doc)
{
  String out;
  serializeJson(doc, out);
  req->send(200, "application/json", out);
}

static void setupCaptivePortalRoutes()
{
  server.onNotFound([](AsyncWebServerRequest *req)
                    { req->send_P(200, "text/html", INDEX_HTML); });
}

// ------------------------------
// webBegin
// ------------------------------
void webBegin(DeviceCfg &cfg, RuntimeState &st, SensorsState &ss)
{
  g_cfg = &cfg;
  g_st = &st;
  g_ss = &ss;

  // Captive DNS only in AP mode (your own app UI)
  if (wifiGetState() == WifiModeState::AP_CAPTIVE)
  {
    dns.start(CAPTIVE_DNS_PORT, "*", WiFi.softAPIP());
  }

  // Root UI
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req)
            { req->send_P(200, "text/html", INDEX_HTML); });

  // Simple ping for debugging
  server.on("/ping", HTTP_GET, [](AsyncWebServerRequest *req)
            { req->send(200, "text/plain", "pong"); });

  // ----------------------------
  // Status
  // ----------------------------
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req)
            {
  StaticJsonDocument<3072> doc;


  doc["time_valid"] = timeIsValid() ? 1 : 0;

  const char* src =
  (timeSource() == TimeSource::NTP)    ? "ntp" :
  (timeSource() == TimeSource::STORED) ? "stored" :
                                        "none";
  doc["time_src"]   = src;

// optional but very useful:
  doc["time_epoch"] = (uint32_t)timeNow();

  doc["app_name"]   = APP_NAME;
  doc["fw_version"] = FW_VERSION;
  doc["fw_build"]   = FW_BUILD;

  doc["device_name"] = g_cfg->deviceName;
  doc["net_mode"]    = (wifiGetState() == WifiModeState::STA_CONNECTED) ? "STA" : "AP";
  doc["ip"]          = wifiIpString();
  doc["ap_ssid"]     = g_cfg->apSsid;

  doc["voltage_v"]   = g_ss->voltageV;
  doc["tank_ml"]     = g_cfg->tankLevelMl;
  doc["usage_ml"]    = g_cfg->usageMl;
  doc["tank_total_ml"] = g_cfg->resetLevelMl;
  doc["flow_ml_per_sec"] = g_cfg->flowMlPerSec;
  doc["return_flow_ml_per_sec"] = g_cfg->returnFlowMlPerSec;
  doc["actual_flow_ml_per_sec"] = g_cfg->actualFlowMlPerSec();
  doc["time_zone"] = g_cfg->timeZone;
  doc["notify_email"] = g_cfg->notifyEmail;
  doc["notify_low_tank"] = g_cfg->notifyLowTank;
  doc["notify_errors"] = g_cfg->notifyErrors;
  doc["notify_status"] = g_cfg->notifyStatus;
  doc["mqtt_host"] = g_cfg->mqttHost;
  doc["mqtt_port"] = g_cfg->mqttPort;
  doc["mqtt_user"] = g_cfg->mqttUser;
  doc["mqtt_pass_set"] = !g_cfg->mqttPass.isEmpty();
  doc["smtp_host"] = g_cfg->smtpHost;
  doc["smtp_port"] = g_cfg->smtpPort;
  doc["smtp_user"] = g_cfg->smtpUser;
  doc["smtp_from"] = g_cfg->smtpFrom;
  doc["smtp_ssl"] = g_cfg->smtpUseSsl;
  doc["smtp_pass_set"] = !g_cfg->smtpPass.isEmpty();
  doc["pump_on"]     = g_st->pumpOn;
  doc["pump_remaining_s"] = pumpRemainingSeconds(*g_st);
  doc["pump_requested_s"] = g_st->requestedRunS;
  doc["boot_ready"]  = g_st->bootReady;
  doc["time_hhmm"]   = hhmmString();

  // ---- Last watering (RAM log) ----
  doc["last_start_ts"] = (uint32_t)g_st->lastStartTs;
  doc["last_stop_ts"]  = (uint32_t)g_st->lastStopTs;
  doc["last_run_s"]    = (uint32_t)g_st->lastRunS;
  doc["last_reason"]   = (const char*)g_st->lastReason;

  JsonObject m = doc.createNestedObject("morning");
  addScheduleJson(m, g_cfg->morning);

  JsonObject e = doc.createNestedObject("evening");
  addScheduleJson(e, g_cfg->evening);

  sendJson(req, doc); });

  // ----------------------------
  // Update schedules - Morning
  // ----------------------------
  server.on("/api/schedule/morning", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr, [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t)
            {

      StaticJsonDocument<512> in;
      StaticJsonDocument<256> out;

      auto err = deserializeJson(in, data, len);
      if (err) {
        out["ok"]  = false;
        out["err"] = "bad_json";
        sendJson(req, out);
        return;
      }

      uint16_t stt = in["start_hhmm"] | g_cfg->morning.startHHMM;
      uint8_t  run = in["run_min"]    | g_cfg->morning.runMin;
      bool     en  = in["enabled"]    | g_cfg->morning.enabled;
      uint8_t  days = sanitizeDaysMask(in["days_mask"] | g_cfg->morning.daysMask);

      if (run < RUN_MIN_MINUTES) run = RUN_MIN_MINUTES;
      if (run > RUN_MAX_MINUTES) run = RUN_MAX_MINUTES;
      if (stt > 2359) stt = 2359;

      g_cfg->morning.startHHMM = stt;
      g_cfg->morning.runMin    = run;
      g_cfg->morning.enabled   = en;
      g_cfg->morning.daysMask  = days;

      saveConfig(*g_cfg);

      out["ok"] = true;
      sendJson(req, out); });

  // ----------------------------
  // Update schedules - Evening
  // ----------------------------
  server.on("/api/schedule/evening", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr, [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t)
            {

      StaticJsonDocument<512> in;
      StaticJsonDocument<256> out;

      auto err = deserializeJson(in, data, len);
      if (err) {
        out["ok"]  = false;
        out["err"] = "bad_json";
        sendJson(req, out);
        return;
      }

      uint16_t stt = in["start_hhmm"] | g_cfg->evening.startHHMM;
      uint8_t  run = in["run_min"]    | g_cfg->evening.runMin;
      bool     en  = in["enabled"]    | g_cfg->evening.enabled;
      uint8_t  days = sanitizeDaysMask(in["days_mask"] | g_cfg->evening.daysMask);

      if (run < RUN_MIN_MINUTES) run = RUN_MIN_MINUTES;
      if (run > RUN_MAX_MINUTES) run = RUN_MAX_MINUTES;
      if (stt > 2359) stt = 2359;

      g_cfg->evening.startHHMM = stt;
      g_cfg->evening.runMin    = run;
      g_cfg->evening.enabled   = en;
      g_cfg->evening.daysMask  = days;

      saveConfig(*g_cfg);

      out["ok"] = true;
      sendJson(req, out); });

  // ----------------------------
  // Pump on/off
  // ----------------------------
  server.on("/api/pump", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr, [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t)
            {

      StaticJsonDocument<256> in;
      StaticJsonDocument<192> out;

      auto err = deserializeJson(in, data, len);
      if (err) {
        out["ok"]  = false;
        out["err"] = "bad_json";
        sendJson(req, out);
        return;
      }

      int state = in["state"] | 0;

      if (state) {
        // manual ON: enforce max runtime as safety
        pumpStartForMinutes(*g_st, RUN_MAX_MINUTES, "MANUAL_UI");
      } else {
        pumpStopWithReason(*g_st, "MANUAL_OFF");
      }

      out["ok"] = true;
      sendJson(req, out); });

  // ----------------------------
  // Run now (Morning/Evening)
  // ----------------------------
  server.on("/api/runNow", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr, [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t)
            {

      StaticJsonDocument<256> in;
      StaticJsonDocument<256> out;

      auto err = deserializeJson(in, data, len);
      if (err) {
        out["ok"]  = false;
        out["err"] = "bad_json";
        sendJson(req, out);
        return;
      }

      String which = (const char*)(in["which"] | "Morning");

      if (g_cfg->tankLevelMl <= g_cfg->minLevelMl) {
        out["ok"]  = false;
        out["err"] = "low_tank";
        sendJson(req, out);
        return;
      }

      strncpy(g_st->lastReason, "MANUAL_UI", sizeof(g_st->lastReason)-1);
      g_st->lastReason[sizeof(g_st->lastReason)-1] = 0;
      if (timeIsValid()) {
      g_st->lastStartTs = timeNow();
}
      g_st->lastStopTs = 0;
      g_st->lastRunS   = 0;

      if (which == "Morning") pumpStartForMinutes(*g_st, g_cfg->morning.runMin, "MANUAL_UI");
else                    pumpStartForMinutes(*g_st, g_cfg->evening.runMin, "MANUAL_UI");

      out["ok"] = true;
      sendJson(req, out); });

  // ----------------------------
  // Tank reset
  // ----------------------------
  server.on("/api/tankReset", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr, [](AsyncWebServerRequest *req, uint8_t *, size_t, size_t, size_t)
            {

      StaticJsonDocument<128> out;

      g_cfg->tankLevelMl = g_cfg->resetLevelMl;
      saveConfig(*g_cfg);

      out["ok"] = true;
      sendJson(req, out); });

  // ----------------------------
  // Email test
  // ----------------------------
  server.on("/api/email/test", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr, [](AsyncWebServerRequest *req, uint8_t *, size_t, size_t, size_t)
            {

      StaticJsonDocument<192> out;

      if (g_cfg->notifyEmail.isEmpty()) {
        out["ok"] = false;
        out["err"] = "missing_notify_email";
        sendJson(req, out);
        return;
      }

      if (g_cfg->smtpHost.isEmpty()) {
        out["ok"] = false;
        out["err"] = "missing_smtp_host";
        sendJson(req, out);
        return;
      }

      const bool ok = mqttSendTestEmail(*g_cfg);
      out["ok"] = ok;
      if (!ok) out["err"] = "send_failed";
      sendJson(req, out); });

  // ----------------------------
  // WiFiManager controls
  // ----------------------------
  server.on("/api/wifi/setup", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr, [](AsyncWebServerRequest *req, uint8_t *, size_t, size_t, size_t)
            {

      StaticJsonDocument<192> out;
      out["ok"]  = true;
      out["msg"] = "rebooting_into_setup_mode";
      sendJson(req, out);

      delay(250);
      wifiEnterSetupMode(); });

  server.on("/api/wifi/reset", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr, [](AsyncWebServerRequest *req, uint8_t *, size_t, size_t, size_t)
            {

      StaticJsonDocument<128> out;
      out["ok"]  = true;
      out["msg"] = "resetting";
      sendJson(req, out);

      delay(250);
      wifiResetCredentials(); });

  server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *req)
            {
      std::vector<String> networks = wifiScanNetworks();
      StaticJsonDocument<2048> doc;
      JsonArray arr = doc.createNestedArray("networks");
      for (const auto& network : networks) {
        arr.add(network);
      }
      sendJson(req, doc); });

  server.on("/api/wifi/connect", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr, [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t)
            {

      StaticJsonDocument<256> in;
      StaticJsonDocument<192> out;

      auto err = deserializeJson(in, data, len);
      if (err) {
        out["ok"] = false;
        out["err"] = "bad_json";
        sendJson(req, out);
        return;
      }

      String ssid = (const char*)(in["ssid"] | "");
      String pass = (const char*)(in["pass"] | "");
      ssid.trim();

      if (ssid.isEmpty()) {
        out["ok"] = false;
        out["err"] = "missing_ssid";
        sendJson(req, out);
        return;
      }

      if (wifiTryConnect(ssid, pass, g_cfg->deviceName)) {
        out["ok"] = true;
        out["ip"] = wifiIpString();
      } else {
        out["ok"] = false;
        out["err"] = "connect_failed";
      }

      sendJson(req, out); });

  // ----------------------------
  // Device settings (name + AP creds)
  // ----------------------------
  server.on("/api/device", HTTP_POST, [](AsyncWebServerRequest *req) {}, nullptr, [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t, size_t)
            {

      StaticJsonDocument<512> in;
      StaticJsonDocument<192> out;

      auto err = deserializeJson(in, data, len);
      if (err) {
        out["ok"]  = false;
        out["err"] = "bad_json";
        sendJson(req, out);
        return;
      }

      auto setStringIfPresent = [](String& target, JsonVariant v, bool allowEmpty){
        if (v.isNull() || !v.is<const char*>()) return;
        const char* s = v.as<const char*>();
        if (!s) return;
        if (allowEmpty || *s) target = String(s);
      };

      setStringIfPresent(g_cfg->deviceName, in["device_name"], false);
      setStringIfPresent(g_cfg->apSsid,     in["ap_ssid"], false);
      setStringIfPresent(g_cfg->apPass,     in["ap_pass"], false);
      setStringIfPresent(g_cfg->notifyEmail, in["notify_email"], true);
      setStringIfPresent(g_cfg->mqttHost, in["mqtt_host"], true);
      setStringIfPresent(g_cfg->mqttUser, in["mqtt_user"], true);
      setStringIfPresent(g_cfg->mqttPass, in["mqtt_pass"], true);
      setStringIfPresent(g_cfg->smtpHost, in["smtp_host"], true);
      setStringIfPresent(g_cfg->smtpUser, in["smtp_user"], true);
      setStringIfPresent(g_cfg->smtpPass, in["smtp_pass"], true);
      setStringIfPresent(g_cfg->smtpFrom, in["smtp_from"], true);

      if (in.containsKey("notify_low_tank")) g_cfg->notifyLowTank = in["notify_low_tank"];
      if (in.containsKey("notify_errors"))  g_cfg->notifyErrors  = in["notify_errors"];
      if (in.containsKey("notify_status"))  g_cfg->notifyStatus  = in["notify_status"];
      if (in.containsKey("smtp_ssl")) g_cfg->smtpUseSsl = in["smtp_ssl"];

      uint16_t mqttPort = in["mqtt_port"] | g_cfg->mqttPort;
      uint16_t smtpPort = in["smtp_port"] | g_cfg->smtpPort;
      if (mqttPort > 0) g_cfg->mqttPort = mqttPort;
      if (smtpPort > 0) g_cfg->smtpPort = smtpPort;

      // Timezone
      if (in.containsKey("time_zone")) {
        const char* tz = in["time_zone"];
        if (tz && *tz) {
          g_cfg->timeZone = String(tz);
          timeSetTimezone(tz);
        }
      }

      // Tank total (reset level)
      if (in.containsKey("tank_total_ml")) {
        float val = in["tank_total_ml"];
        if (val > 0) {
          g_cfg->resetLevelMl = val;
        }
      }

      if (in.containsKey("return_flow_ml_per_sec")) {
        float val = in["return_flow_ml_per_sec"];
        if (val < 0.0f) val = 0.0f;
        if (val > g_cfg->flowMlPerSec) val = g_cfg->flowMlPerSec;
        g_cfg->returnFlowMlPerSec = val;
      }

      saveConfig(*g_cfg);

      out["ok"] = true;
      sendJson(req, out); });

  setupCaptivePortalRoutes();
  server.begin();
}

// ------------------------------
// webLoop
// ------------------------------
void webLoop()
{
  if (wifiGetState() == WifiModeState::AP_CAPTIVE)
  {
    dns.processNextRequest();
  }
}
