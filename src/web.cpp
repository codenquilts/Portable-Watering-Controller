// web.cpp — clean replacement (WiFiManager setup-mode, no /api/scan, no old /api/wifi save)

#include "web.h"

#include "scheduler.h" // RuntimeState + pump functions
#include "tank.h"      // SensorsState

#include "config.h"
#include "wifi_mgr.h"
#include "ui_index_html.h"
#include "storage.h"

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
  StaticJsonDocument<2048> doc;

  

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
  doc["notify_email"] = g_cfg->notifyEmail;
  doc["notify_low_tank"] = g_cfg->notifyLowTank;
  doc["notify_errors"] = g_cfg->notifyErrors;
  doc["notify_status"] = g_cfg->notifyStatus;
  doc["pump_on"]     = g_st->pumpOn;
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
        strncpy(g_st->lastReason, "MANUAL_UI", sizeof(g_st->lastReason)-1);
        g_st->lastReason[sizeof(g_st->lastReason)-1] = 0;

        if (timeIsValid()) {
         g_st->lastStartTs = timeNow();
         }
        g_st->lastStopTs = 0;
        g_st->lastRunS   = 0;

        // manual ON: enforce max runtime as safety
        pumpStartForMinutes(*g_st, RUN_MAX_MINUTES, "MANUAL_UI");pumpStartForMinutes(*g_st, RUN_MAX_MINUTES, "MANUAL_UI");
      } else {
        if (timeIsValid()) {
        g_st->lastStopTs = timeNow();
        if (g_st->lastStartTs > 0 && g_st->lastStopTs > g_st->lastStartTs) {
       g_st->lastRunS = (uint32_t)(g_st->lastStopTs - g_st->lastStartTs);
  }
}
        pumpStop(*g_st);
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

      auto setIfNonEmpty = [](String& target, JsonVariant v){
        if (!v.is<const char*>()) return;
        const char* s = v.as<const char*>();
        if (s && *s) target = String(s);
      };

      setIfNonEmpty(g_cfg->deviceName, in["device_name"]);
      setIfNonEmpty(g_cfg->apSsid,     in["ap_ssid"]);
      setIfNonEmpty(g_cfg->apPass,     in["ap_pass"]);
      setIfNonEmpty(g_cfg->notifyEmail, in["notify_email"]);

      if (in.containsKey("notify_low_tank")) g_cfg->notifyLowTank = in["notify_low_tank"];
      if (in.containsKey("notify_errors"))  g_cfg->notifyErrors  = in["notify_errors"];
      if (in.containsKey("notify_status"))  g_cfg->notifyStatus  = in["notify_status"];

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
