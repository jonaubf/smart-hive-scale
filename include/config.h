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

// SNTP wait after WiFi STA connect (WiFi mode's clock source, mirroring
// GSM's NITZ/NTP). On a LAN this normally completes in well under a second.
#ifndef WIFI_SNTP_TIMEOUT_MS
#define WIFI_SNTP_TIMEOUT_MS 10000UL
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

// PubSubClient's 15s default leaves only keepalive*1.5 = ~23s of broker
// grace, measured from the last packet Mosquitto actually received. That is
// thin for a 2G uplink where a publish can stall on a bad moment; 60s (90s
// grace) matches what other clients on this broker already negotiate. Note
// this does NOT by itself stop "exceeded timeout" lines in mosquitto.log —
// those come from a lost DISCONNECT packet, see MODEM_TX_DRAIN_DISCONNECT_MS.
constexpr uint16_t MQTT_KEEPALIVE_SEC = 60;

// The SIM800 buffers TX data, and TinyGSM closes its socket with
// AT+CIPCLOSE=<mux>,1 — a *quick* close that discards anything the radio
// has not yet put on the air. Pump the modem before letting a close happen,
// or the tail of a session never leaves the device (2G uplinks are slow
// enough for this to be routine, not an edge case).
constexpr unsigned long MODEM_TX_DRAIN_PAYLOAD_MS = 3000UL;
// Same hazard for the 2-byte MQTT DISCONNECT (a ~30-byte TLS record) — far
// smaller than a telemetry payload, so it needs much less flush time.
constexpr unsigned long MODEM_TX_DRAIN_DISCONNECT_MS = 1000UL;

// Battery ADC. Vbat is nominally divided 2:1 (100k/100k) before the ADC
// input, but the naive raw-to-voltage conversion below doesn't use ESP32's
// factory ADC calibration, and real divider resistors have their own
// tolerance. Single-point-calibrated on this board (plan E2, 2026-08-07):
// old ratio 2.1314 reported 4.026 V vs. 4.112 V on the multimeter ⇒
// 2.1314 * 4.112 / 4.026 = 2.1769. Recalibrate the same way if the divider
// resistors or the board ever change.
constexpr float BATTERY_ADC_REF_V = 3.3f;
constexpr float BATTERY_ADC_MAX = 4095.0f;
constexpr float BATTERY_DIVIDER_RATIO = 2.1769f;
constexpr uint8_t BATTERY_ADC_SAMPLES = 16;
constexpr float BATTERY_EMPTY_V = 3.30f;
constexpr float BATTERY_FULL_V = 4.20f;

// 4 load cells, one per hive-stand corner, each behind its own NAU7802.
// All NAU7802s share the fixed I2C address 0x2A, so each sits on its own
// PCA9548A mux channel; the mux itself is upstream on the shared bus.
constexpr uint8_t NUM_CORNERS = 4;
constexpr uint8_t I2C_MUX_ADDR = 0x70;  // PCA9548A, A0/A1/A2 tied to GND
// Corner index (0..NUM_CORNERS-1, the order corner1_kg..corner4_kg is
// reported in) -> physical PCA9548A channel, as actually wired.
constexpr uint8_t CORNER_MUX_CHANNEL[NUM_CORNERS] = {0, 1, 2, 7};

// SIM800L rail control (PIN_MODEM_EN -> CR-SJ5530 #2's EN). No PWRKEY on
// this module — it auto-boots when the rail comes up, and powers off by
// dropping the whole rail (AT+CPOWD=1 first for a clean network detach).
constexpr unsigned long MODEM_EN_SETTLE_MS = 100UL;       // rail rise/settle before first UART traffic
constexpr unsigned long MODEM_BOOT_TIMEOUT_MS = 10000UL;  // wait for the module to answer AT after EN high
constexpr unsigned long MODEM_CPOWD_DETACH_MS = 3000UL;   // CPOWD -> network detach window before EN low

// NAU7802: gain 128, 10 SPS, internal LDO 3.0 V for bridge excitation.
constexpr uint8_t SCALE_RAW_SAMPLES = 10;
// After deep sleep the NAU7802 is power-cycled; the first conversions after
// power-up/AFE calibration are off, so discard them before measuring.
constexpr uint8_t SCALE_WARMUP_READS = 5;
// ADC/load cell readings drift ~50 g during the first minutes after
// power-up (self-heating). Scheduled reports wait this long since boot
// before measuring so every report is taken at the same thermal state.
constexpr unsigned long SCALE_THERMAL_WARMUP_MS = 2UL * 60UL * 1000UL;
// Live bench/portal display samples *per corner*: a full-precision
// SCALE_RAW_SAMPLES pass over 4 corners takes ~4 s at 10 SPS, too slow for
// a 2 s refresh — 3 per corner (~1.2 s total) keeps the display responsive.
// Publish-cycle snapshots still use SCALE_RAW_SAMPLES per corner.
constexpr uint8_t SCALE_LIVE_SAMPLES = 3;
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

// WiFi's radio teardown (esp_wifi_stop()) right before deep sleep draws a
// brief current spike. Over USB this is invisible (USB backs the rail); on
// battery it appears to brown out the board before it ever reaches its first
// RTC wake. This settle window after teardown, before the rest of the
// power-down sequence continues, is an experiment to let the supply recover.
constexpr unsigned long RADIO_TEARDOWN_SETTLE_MS = 500UL;

constexpr const char *MQTT_TOPIC_STATE = "beekpr/" DEVICE_ID "/state";
constexpr const char *MQTT_TOPIC_AVAILABILITY = "beekpr/" DEVICE_ID "/availability";
