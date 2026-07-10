# TTGO T-Call V1.3 — hardware reference

Board used by Smart Hive Scale: ESP32-WROVER-B + SIM800L + IP5306 PMIC.

## Pinout

![TTGO T-Call V1.3 pinout](T-Call.jpg)

## Wiring (this project)

![Wiring: T-Call, NAU7802, DS18B20, load cell, setup button](tcall_nau7802_wiring.svg)

| Connection | Pins |
|------------|------|
| NAU7802 SCL / SDA | GPIO 18 / GPIO 19 (I2C 0x2A) |
| NAU7802 VIN / GND | 3.3 V / GND |
| DS18B20 DQ | GPIO 25 |
| DS18B20 VDD / GND | 3.3 V / GND |
| DS18B20 pull-up | **4.7 kOhm** from DQ to 3.3 V (required) |
| Setup button | GPIO 13 ↔ GND (NO, internal pull-up) |
| Battery sense | GPIO 35 (ADC, onboard) |

The NAU7802 uses its own I2C bus on GPIO 18/19 — GPIO 21/22 belong to the onboard IP5306 PMIC and must not be shared. Bridge excitation comes from the NAU7802's internal LDO (3.0 V), so the load cell needs no separate supply.

**Previous setup (removed):** HX711 on GPIO 32/33 — see [legacy wiring](tcall_hx711_wiring.svg).

**Reserved — do not use for sensors:** GPIO 4, 5, 21, 22, 23, 26, 27 (modem + PMIC).

## Official resources

- [LilyGO T-Call SIM800 (GitHub)](https://github.com/xinyuan-lilygo/lilygo-t-call-sim800)
- Full tables: [`local-setup.md` — Wiring](local-setup.md#wiring)
- Project spec: [`spec.md` §10](../spec.md#10-hardware-connections)
