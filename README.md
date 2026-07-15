# Smart Hive Scale

Battery-powered beehive weight monitor for remote apiaries. An ESP32 reads a load cell, wakes on a schedule, and publishes telemetry to **Home Assistant** over **2G GPRS** (field) or **WiFi** (at home).

Built for **TTGO T-Call V1.3** (ESP32 + SIM800L), NAU7802 + 200 kg load cell, DS18B20 scale temperature sensor, deep-sleep power management, and a web config portal for calibration and settings without reflashing.

## Features

- Weight measurement with tare/calibration (serial or web portal)
- MQTT JSON telemetry (`weight_kg`, `stable_kg`, `temp_scale_c`, battery, GSM/WiFi status, cell tower IDs)
- **GSM mode** — TLS MQTT over public IP (port 8883) for field hives
- **WiFi mode** — plain MQTT on LAN (port 1883) when the hive is at home
- Deep sleep between reports (default every 6 hours, configurable)
- Setup button → WiFi AP config portal (calibration, broker, OTA firmware update)
- Home Assistant integration via MQTT sensors (one device per hive)

## Quick start

```bash
git clone <repo-url> beekpr-weights
cd beekpr-weights
cp .env.example .env          # edit MQTT credentials and broker host
python3 -m venv .venv
.venv/bin/python -m pip install -U pip platformio
.venv/bin/pio run -t upload
.venv/bin/pio device monitor -b 115200
```

See **[Local development setup](doc/local-setup.md)** for wiring, calibration, and bench commands.

## Documentation

| Guide | Description |
|-------|-------------|
| [**User guide**](doc/user-guide.md) | End-to-end: calibrate, connect MQTT, Home Assistant, daily use |
| [**Local setup**](doc/local-setup.md) | Build, flash, wiring, serial commands, config portal |
| [**T-Call hardware**](doc/esp32_sim800l.md) | Board pinout and NAU7802/DS18B20 wiring diagrams |
| [**MQTT & TLS setup**](doc/mqtt-tls-setup.md) | Mosquitto certificates, port forward, field device security |
| [**Project specification**](spec.md) | Requirements, architecture, implementation plan |
| [**Home Assistant YAML**](doc/home-assistant/mqtt_sensors.yaml) | MQTT entities grouped under one device per hive |

## Hardware

- TTGO T-Call V1.3 + 2G SIM (nano)
- YZC-1B 200 kg load cell + Adafruit NAU7802 (I2C, SDA **GPIO 19**, SCL **GPIO 18**, 3.3 V)
- DS18B20 temperature sensor on the scale frame (DQ **GPIO 25**, 4.7 kΩ pull-up to 3.3 V)
- Li-Ion battery, outdoor enclosure, GSM antenna
- Setup button (GPIO 13 → GND)

![Wiring: T-Call, NAU7802, DS18B20, DS3231, load cell, setup button](doc/tcall_nau7802_wiring.svg)

Pin map, mechanical notes, and full tables: [spec §10](spec.md#10-hardware-connections) and [local-setup wiring](doc/local-setup.md#wiring).

## License

See [LICENSE](LICENSE).
