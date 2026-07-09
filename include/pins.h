#pragma once

// TTGO T-Call V1.3 — onboard SIM800L (do not use for other peripherals)
constexpr int PIN_MODEM_TX = 27;       // ESP32 TX -> modem RX
constexpr int PIN_MODEM_RX = 26;       // ESP32 RX <- modem TX
constexpr int PIN_MODEM_PWRKEY = 4;
constexpr int PIN_MODEM_RST = 5;
constexpr int PIN_MODEM_POWER = 23;

// IP5306 PMIC I2C — reserved
constexpr int PIN_PMIC_SDA = 21;
constexpr int PIN_PMIC_SCL = 22;

// HX711 load cell ADC. GPIO 32+33 are adjacent on the header, RTC-capable
// (SCK can be held high across deep sleep) and have no boot/strapping role
// (unlike GPIO 12, which selects flash voltage at reset).
constexpr int PIN_HX711_DT = 33;
constexpr int PIN_HX711_SCK = 32;

// Battery voltage sense (ADC1, input-only)
constexpr int PIN_BATTERY_ADC = 35;

// Setup / config portal button (NO to GND, internal pull-up)
constexpr int PIN_SETUP_BUTTON = 13;
