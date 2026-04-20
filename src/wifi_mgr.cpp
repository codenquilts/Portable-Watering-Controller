#include "wifi_mgr.h"
#include "config.h"

#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include "time_mgr.h"

static WifiModeState g_state = WifiModeState::AP_CAPTIVE;
static bool g_mdnsStarted = false;

static Preferences prefs;
static const char* PREF_NS  = "wifi";
static const char* KEY_SETUP= "setup"; // bool

static WiFiManager wm;

static String sanitizeMdnsName(const String& name) {
  String host = name;
  host.toLowerCase();
  host.trim();
  if (host.length() == 0) host = "watering";
  for (size_t i = 0; i < host.length(); i++) {
    char c = host[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-');
    if (!ok) host.setCharAt(i, '-');
  }
  return host;
}

static void mdnsStart(const String& deviceName) {
  if (g_mdnsStarted) return;
  String host = sanitizeMdnsName(deviceName);
  if (MDNS.begin(host.c_str())) {
    MDNS.addService("http", "tcp", 80);
    g_mdnsStarted = true;
    Serial.printf("mDNS started: http://%s.local/\n", host.c_str());
  } else {
    Serial.println("mDNS start failed");
  }
}

static void mdnsStop() {
  if (!g_mdnsStarted) return;
  MDNS.end();
  g_mdnsStarted = false;
}

bool wifiIsSetupMode() {
  prefs.begin(PREF_NS, true);
  bool v = prefs.getBool(KEY_SETUP, false);
  prefs.end();
  return v;
}

void wifiEnterSetupMode() {
  prefs.begin(PREF_NS, false);
  prefs.putBool(KEY_SETUP, true);
  prefs.end();
  delay(150);
  ESP.restart();
}

void wifiClearSetupModeFlag() {
  prefs.begin(PREF_NS, false);
  prefs.putBool(KEY_SETUP, false);
  prefs.end();
}

void wifiResetCredentials() {
  Serial.println("WiFiManager: reset settings + reboot");
  wm.resetSettings();
  WiFi.disconnect(true, true);
  delay(200);
  ESP.restart();
}

WifiModeState wifiBegin(DeviceCfg& cfg) {
  mdnsStop();
  WiFi.setSleep(false);

  String host = sanitizeMdnsName(cfg.deviceName);
  WiFi.setHostname(host.c_str());

  wm.setDebugOutput(true);
  wm.setConnectTimeout(20);

  String apSsid = cfg.apSsid.length() ? cfg.apSsid : String(DEFAULT_AP_SSID);
  String apPass = cfg.apPass.length() ? cfg.apPass : String(DEFAULT_AP_PASS);

  // ---- SETUP MODE: portal only (blocks until configured) ----
  if (wifiIsSetupMode()) {
    Serial.printf("SETUP MODE: starting WiFiManager portal on AP '%s' (port 80)\n", apSsid.c_str());

    // Stay in portal until user configures WiFi
    wm.setConfigPortalTimeout(0);

    bool ok = wm.startConfigPortal(apSsid.c_str(), apPass.c_str());

    // If user saved WiFi, we should now be connected
    if (ok && WiFi.status() == WL_CONNECTED) {
  Serial.printf("WiFi configured, IP=%s\n", WiFi.localIP().toString().c_str());
  wifiClearSetupModeFlag();
  g_state = WifiModeState::STA_CONNECTED;

  timeOnWifiConnected();          // <-- ADD THIS (starts NTP, non-blocking)

  mdnsStart(cfg.deviceName);
  return g_state;
}

    // If portal exited for any reason, remain AP_CAPTIVE
    g_state = WifiModeState::AP_CAPTIVE;
    return g_state;
  }

  // ---- NORMAL MODE: try connect quickly; if fails, fall back to AP (your app handles captive UI) ----
  Serial.println("NORMAL MODE: trying saved WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(); // use stored creds

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(150);
  }

  if (WiFi.status() == WL_CONNECTED) {
  g_state = WifiModeState::STA_CONNECTED;
  Serial.printf("WiFi connected, IP=%s\n", WiFi.localIP().toString().c_str());

  timeOnWifiConnected();          // start NTP (non-blocking)
  Serial.println("NTP start: timeOnWifiConnected()");

  mdnsStart(cfg.deviceName);
  return g_state;
}

  // AP fallback (your own UI on port 80)
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid.c_str(), apPass.c_str());

  g_state = WifiModeState::AP_CAPTIVE;
  Serial.printf("AP mode: SSID='%s' IP=%s\n", apSsid.c_str(), WiFi.softAPIP().toString().c_str());
  return g_state;
}

WifiModeState wifiGetState() { return g_state; }

String wifiIpString() {
  if (g_state == WifiModeState::STA_CONNECTED) return WiFi.localIP().toString();
  return WiFi.softAPIP().toString();
}
