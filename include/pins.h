#pragma once

// Discrete ESP32-WROOM-32 build (spec.md §10). Bare devkit — no onboard
// modem/PMIC, so no board-specific reserved pins. Avoid only the universal
// ESP32 ones: strapping pins 0/2/12/15, UART0 1/3, internal flash 6-11.

// SIM800L over UART2 (TX2/RX2 silk labels on 30-pin devkits).
constexpr int PIN_MODEM_TX = 17;  // ESP32 TX2 -> SIM800L RXD, via 1k series + 5.6k shunt divider (3.3V -> ~2.8V)
constexpr int PIN_MODEM_RX = 16;  // ESP32 RX2 <- SIM800L TXD, via 1k series (protection only)

// This SIM800L board has no PWRKEY pin (tied to GND internally — the module
// auto-boots the moment VCC appears). Power control is at the rail instead:
// GPIO 4 drives CR-SJ5530 #2's EN pin (EN low = whole converter off).
// RTC-domain pin — must be latched low with gpio_hold_en() through deep
// sleep, or it floats, EN re-enables, and the modem drains the battery all
// through the sleep window (spec.md §10).
constexpr int PIN_MODEM_EN = 4;
constexpr int PIN_MODEM_RST = 5;  // active low; emergency reset only (normal power-off is AT+CPOWD=1 + EN low)

// Single shared I2C bus: PCA9548A mux (0x70) and DS3231 (0x68) upstream,
// 4x NAU7802 (all fixed at 0x2A) behind mux channels CORNER_MUX_CHANNEL
// (config.h — currently 0/1/2/7 as wired).
constexpr int PIN_I2C_SDA = 21;
constexpr int PIN_I2C_SCL = 22;

// DS18B20 scale/frame temperature, OneWire with external 4.7 kOhm pull-up
// from DQ to 3.3 V.
constexpr int PIN_TEMP_ONEWIRE = 25;

// Battery voltage sense (ADC1, input-only) — external 100k/100k divider
// from the raw battery node, ratio calibrated per board (config.h).
constexpr int PIN_BATTERY_ADC = 35;

// Setup / config portal button (NO to GND, internal pull-up)
constexpr int PIN_SETUP_BUTTON = 13;

// DS3231 RTC alarm (SQW/INT), open-drain with the module's own pull-up;
// ext1 deep-sleep wake for the report schedule.
constexpr int PIN_RTC_ALARM = 14;
