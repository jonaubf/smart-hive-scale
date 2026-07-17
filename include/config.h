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
// Vbat is nominally divided 2:1 before the ADC input, but the naive
// raw-to-voltage conversion below doesn't use ESP32's factory ADC
// calibration, and real divider resistors have their own tolerance — so the
// nominal 2.0 ratio measured ~6.6% low against a multimeter on this unit
// (reported 3.85V vs. actual 4.103V). Single-point-calibrated against that
// reading (2.0 * 4.103/3.85) rather than assumed from the nominal resistor
// values; re-derive the same way if you swap boards or resistors.
constexpr float BATTERY_ADC_REF_V = 3.3f;
constexpr float BATTERY_ADC_MAX = 4095.0f;
constexpr float BATTERY_DIVIDER_RATIO = 2.1314f;
constexpr uint8_t BATTERY_ADC_SAMPLES = 16;
constexpr float BATTERY_EMPTY_V = 3.30f;
constexpr float BATTERY_FULL_V = 4.20f;

// NAU7802: gain 128, 10 SPS, internal LDO 3.0 V for bridge excitation.
constexpr uint8_t SCALE_RAW_SAMPLES = 10;
// After deep sleep the NAU7802 is power-cycled; the first conversions after
// power-up/AFE calibration are off, so discard them before measuring.
constexpr uint8_t SCALE_WARMUP_READS = 5;
// ADC/load cell readings drift ~50 g during the first minutes after
// power-up (self-heating). Scheduled reports wait this long since boot
// before measuring so every report is taken at the same thermal state.
constexpr unsigned long SCALE_THERMAL_WARMUP_MS = 2UL * 60UL * 1000UL;
// Tare/calibrate: single pass — short settle, then median of these samples
// (~2 s at 10 SPS).
constexpr uint8_t SCALE_CAL_SAMPLES = 20;
constexpr unsigned long SCALE_CAL_SETTLE_MS = 400;
constexpr uint8_t SCALE_DISPLAY_MEDIAN_COUNT = 5;
constexpr unsigned long SCALE_READ_INTERVAL_MS = 2000;

// DS18B20: 12-bit conversion takes up to 750 ms; done synchronously once per
// reading (weight publish or bench print), so latency is acceptable.
constexpr uint8_t TEMP_SENSOR_RESOLUTION_BITS = 12;

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

// On battery, deep sleep longer than ~32 s risks the IP5306 auto-cutting the
// 5 V rail if its "keep boost on" register bit isn't actually held (some
// clones ack the I2C write without the bit sticking). app_scheduler.cpp only
// arms the ESP32's internal timer for this short window — waking to reset
// the PMIC's own shutoff timer, independent of the DS3231-driven report
// schedule — when the verified read-back says the bit didn't take; a
// verified-good PMIC sleeps on the DS3231 alarm alone.
constexpr unsigned long IP5306_KEEPALIVE_CHUNK_SEC = 25UL;

// WiFi's radio teardown (esp_wifi_stop()) right before deep sleep draws a
// brief current spike. Over USB this is invisible (USB backs the rail); on
// battery it appears to brown out the board before it ever reaches its first
// RTC wake. This settle window after teardown, before the rest of the
// power-down sequence continues, is an experiment to let the supply recover.
constexpr unsigned long RADIO_TEARDOWN_SETTLE_MS = 500UL;

constexpr const char *MQTT_TOPIC_STATE = "beekpr/" DEVICE_ID "/state";
constexpr const char *MQTT_TOPIC_AVAILABILITY = "beekpr/" DEVICE_ID "/availability";
