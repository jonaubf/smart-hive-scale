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

// NAU7802 load cell ADC on a dedicated I2C bus (GPIO 21/22 belong to the
// IP5306 PMIC). Address 0x2A, powered from 3.3 V.
constexpr int PIN_SCALE_I2C_SDA = 19;
constexpr int PIN_SCALE_I2C_SCL = 18;

// DS18B20 scale/frame temperature, OneWire with external 4.7 kOhm pull-up
// from DQ to 3.3 V.
constexpr int PIN_TEMP_ONEWIRE = 25;

// Battery voltage sense (ADC1, input-only)
constexpr int PIN_BATTERY_ADC = 35;

// Setup / config portal button (NO to GND, internal pull-up)
constexpr int PIN_SETUP_BUTTON = 13;
