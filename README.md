# Smart Hive Scale

Battery-powered beehive weight monitor for remote apiaries. An ESP32 reads a load cell, wakes on a schedule, and publishes telemetry to **Home Assistant** over **2G GPRS** (field) or **WiFi** (at home).

Built for **TTGO T-Call V1.3** (ESP32 + SIM800L), HX711 + 200 kg load cell, deep-sleep power management, and a web config portal for calibration and settings without reflashing.

## Features

- Weight measurement with tare/calibration (serial or web portal)
- MQTT JSON telemetry (`weight_kg`, `stable_kg`, battery, GSM/WiFi status, cell tower IDs)
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
| [**MQTT & TLS setup**](doc/mqtt-tls-setup.md) | Mosquitto certificates, port forward, field device security |
| [**Project specification**](spec.md) | Requirements, architecture, implementation plan |
| [**Home Assistant YAML**](doc/home-assistant/mqtt_sensors.yaml) | MQTT entities grouped under one device per hive |

## Hardware

- TTGO T-Call V1.3 + 2G SIM (nano)
- YZC-1B 200 kg load cell + HX711
- Li-Ion battery, outdoor enclosure, GSM antenna
- Setup button (GPIO 13 → GND)

Pin map and mechanical notes: [spec §10](spec.md#10-hardware-connections) and [local-setup wiring](doc/local-setup.md#wiring-for-step-2-hx711-bench-test).

## License

See [LICENSE](LICENSE).
