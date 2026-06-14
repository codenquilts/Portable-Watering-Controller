#pragma once
#include <Arduino.h>

// ---------------- Pins ----------------
#ifndef PWB_PIN_RELAY
#define PWB_PIN_RELAY 16
#endif

#ifndef PWB_PIN_RELAY2
#define PWB_PIN_RELAY2 17
#endif

static constexpr uint8_t PIN_RELAY = PWB_PIN_RELAY;
static constexpr uint8_t PIN_RELAY2 = PWB_PIN_RELAY2;

// Relay output logic level (set these to match your relay board)
#ifndef RELAY_ON_LEVEL
#define RELAY_ON_LEVEL HIGH
#endif

#ifndef RELAY_OFF_LEVEL
#define RELAY_OFF_LEVEL LOW
#endif

// ---------------- Status LED ----------------
#ifndef LED_PIN
#define LED_PIN 23 // NOTE: many ESP32 DevKit boards use GPIO2; 23 may be your custom LED
#endif

#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW false
#endif

// ----------------------------
// Firmware identity
// ----------------------------
#define APP_NAME "Portable Watering Bin"
#define FW_VERSION "1.7.0" // <-- change only this when you release
#define FW_BUILD __DATE__ " " __TIME__

// ADC (voltage sense)
#ifndef PWB_PIN_ADC_VBAT
#define PWB_PIN_ADC_VBAT 34
#endif

static constexpr uint8_t PIN_ADC_VBAT = PWB_PIN_ADC_VBAT;

// ---------------- Tank model ----------------
static constexpr float TANK_TOTAL_ML = 55000.0f;
static constexpr float FLOW_ML_PER_SEC = 70.0f;
static constexpr float TANK_MIN_ML = 500.0f;

// ---------------- Scheduler limits ----------------
static constexpr uint8_t RUN_MIN_MINUTES = 1;
static constexpr uint8_t RUN_MAX_MINUTES = 15;
static constexpr uint16_t RUN_MIN_SECONDS = 1;
static constexpr uint16_t RUN_MAX_SECONDS = RUN_MAX_MINUTES * 60U;

// ---------------- Time ----------------
static const char *TZ_INFO = "AEST-10AEDT,M10.1.0/2,M4.1.0/3"; // Melbourne TZ rule
static const char *NTP1 = "pool.ntp.org";
static const char *NTP2 = "time.google.com";

// ---------------- AP defaults ----------------
static const char *DEFAULT_AP_SSID = "Watering-Setup";
static const char *DEFAULT_AP_PASS = "water1234"; // change later

// ---------------- MQTT ----------------
#ifndef MQTT_HOST
#define MQTT_HOST ""
#endif

#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif

#ifndef MQTT_USER
#define MQTT_USER ""
#endif

#ifndef MQTT_PASS
#define MQTT_PASS ""
#endif

// ---------------- Email notifications ----------------
#ifdef HAS_BUILD_CONFIG
#include "build_config.h"
#endif

#ifndef SMTP_HOST
#define SMTP_HOST ""
#endif

#ifndef SMTP_PORT
#define SMTP_PORT 587
#endif

#ifndef SMTP_USER
#define SMTP_USER ""
#endif

#ifndef SMTP_PASS
#define SMTP_PASS ""
#endif

#ifndef SMTP_FROM
#define SMTP_FROM "pwb@example.com"
#endif

#ifndef SMTP_USE_SSL
#define SMTP_USE_SSL 0
#endif

// ---------------- Captive portal ----------------
static constexpr uint16_t CAPTIVE_DNS_PORT = 53;

// ---------------- Loop timings ----------------
static constexpr uint32_t STATUS_PUBLISH_MS = 60 * 1000UL; // 60s
static constexpr uint32_t TANK_TICK_MS = 5 * 1000UL;       // 5s like your rules
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 12 * 1000UL;
