#pragma once

#include "build_env.h"

// Non-sensitive defaults. Override via .env (generated into build_env.h).

#ifndef DEVICE_ID
#define DEVICE_ID "hive-01"
#endif

#ifndef MQTT_BROKER_HOST
#define MQTT_BROKER_HOST "203.0.113.1"
#endif

#ifndef MQTT_BROKER_PORT
#define MQTT_BROKER_PORT 8883
#endif

#ifndef MQTT_USE_TLS
#define MQTT_USE_TLS 1
#endif

#ifndef MQTT_USERNAME
#define MQTT_USERNAME "BEEKPR_MQTT_USER"
#endif

#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD "BEEKPR_MQTT_PASS"
#endif

#ifndef GSM_APN
#define GSM_APN "internet"
#endif

#ifndef GSM_USER
#define GSM_USER ""
#endif

#ifndef GSM_PASS
#define GSM_PASS ""
#endif

#ifndef GSM_PIN
#define GSM_PIN ""
#endif

#ifndef WIFI_AP_PASSWORD
#define WIFI_AP_PASSWORD "beekpr-setup"
#endif

#ifndef SETUP_BUTTON_HOLD_MS
#define SETUP_BUTTON_HOLD_MS 10000UL
#endif

#ifndef WIFI_CONNECT_TIMEOUT_MS
#define WIFI_CONNECT_TIMEOUT_MS 15000UL
#endif

#ifndef MODEM_NETWORK_TIMEOUT_MS
#define MODEM_NETWORK_TIMEOUT_MS 120000UL
#endif

#ifndef MODEM_GPRS_CONNECT_TIMEOUT_MS
#define MODEM_GPRS_CONNECT_TIMEOUT_MS 60000UL
#endif

#ifndef MODEM_TCP_CONNECT_TIMEOUT_MS
#define MODEM_TCP_CONNECT_TIMEOUT_MS 30000UL
#endif

#ifndef MODEM_TLS_HANDSHAKE_TIMEOUT_MS
#define MODEM_TLS_HANDSHAKE_TIMEOUT_MS 120000UL
#endif

#ifndef MODEM_MQTT_CONNECT_TIMEOUT_MS
#define MODEM_MQTT_CONNECT_TIMEOUT_MS 120000UL
#endif

// Battery ADC defaults for TTGO T-Call.
// Vbat is typically divided before ADC input (2:1 on many boards).
constexpr float BATTERY_ADC_REF_V = 3.3f;
constexpr float BATTERY_ADC_MAX = 4095.0f;
constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
constexpr uint8_t BATTERY_ADC_SAMPLES = 16;
constexpr float BATTERY_EMPTY_V = 3.30f;
constexpr float BATTERY_FULL_V = 4.20f;

// HX711: channel A, gain 128, ~10 SPS (library default)
constexpr uint8_t HX711_RAW_SAMPLES = 10;
constexpr uint8_t HX711_TARE_PREVIEW_COUNT = 5;
constexpr uint8_t HX711_CAL_PREVIEW_COUNT = 7;
constexpr uint8_t HX711_DISPLAY_MEDIAN_COUNT = 5;
constexpr unsigned long HX711_TARE_SETTLE_MS = 3000;
constexpr unsigned long HX711_CAL_SETTLE_MS = 2000;
constexpr unsigned long HX711_TARE_INTERVAL_MS = 2000;
constexpr unsigned long HX711_CAL_INTERVAL_MS = 1000;
constexpr unsigned long HX711_READ_INTERVAL_MS = 2000;

// 4 transmissions per day
constexpr unsigned long WAKE_INTERVAL_SEC = 6UL * 60UL * 60UL;

// Publish cycle retries (FR-5): attempt N times with growing backoff,
// then give up and deep sleep until the next scheduled wake.
constexpr uint8_t PUBLISH_MAX_ATTEMPTS = 3;
constexpr unsigned long PUBLISH_RETRY_BASE_DELAY_MS = 30000UL;

// After power-on or button wake the device stays in bench mode (serial
// commands, live readings, portal) before publishing and going to sleep.
// Any serial input extends the window.
constexpr unsigned long BENCH_STAY_AWAKE_MS = 5UL * 60UL * 1000UL;

constexpr const char *MQTT_TOPIC_STATE = "beekpr/" DEVICE_ID "/state";
constexpr const char *MQTT_TOPIC_AVAILABILITY = "beekpr/" DEVICE_ID "/availability";
