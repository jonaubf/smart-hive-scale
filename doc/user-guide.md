# Smart Hive Scale — User Guide

End-to-end guide: install the hive scale, calibrate weight, connect MQTT, and view everything in Home Assistant under **one device per hive**.

**Related docs**

| Topic | Document |
|-------|----------|
| Build, flash, bench commands | [`local-setup.md`](local-setup.md) |
| T-Call pinout and wiring diagrams | [`esp32_sim800l.md`](esp32_sim800l.md) |
| Mosquitto TLS, certificates, port forward | [`mqtt-tls-setup.md`](mqtt-tls-setup.md) |
| Home Assistant MQTT entities (YAML) | [`home-assistant/mqtt_sensors.yaml`](home-assistant/mqtt_sensors.yaml) |

---

## 1. What you get

Each hive has a battery-powered TTGO T-Call scale that:

1. Wakes on a timer (default every **6 hours**, configurable).
2. Measures weight and battery.
3. Connects over **WiFi** (at home) or **2G GPRS** (in the field).
4. Publishes JSON to your Mosquitto broker.
5. Goes back to **deep sleep** until the next cycle.

Home Assistant shows weight, battery, connectivity, and cell tower info on a single device card — no custom integration required.

---

## 2. Hardware checklist

| Item | Notes |
|------|-------|
| TTGO T-Call V1.3 | ESP32 + SIM800L + IP5306 PMIC |
| YZC-1B 200 kg load cell + NAU7802 | I2C ADC, wired per [`local-setup.md`](local-setup.md) |
| DS18B20 temperature sensor | On the scale frame, GPIO 25 + 4.7 kΩ pull-up |
| Li-Ion battery | 18650 or pack, 3000–6000 mAh |
| 2G nano SIM | Data plan (Kyivstar default APN `internet`) |
| GSM antenna | Outside the enclosure |
| Setup button | NO push button: GPIO 13 ↔ GND |
| IP65 enclosure | Outdoor mounting |

![Wiring diagram](tcall_nau7802_wiring.svg)

![TTGO T-Call V1.3 pinout](T-Call.jpg)

Full wiring tables and pin notes: [`local-setup.md` — Wiring](local-setup.md#wiring).

Mount the load cell in **compression** between a fixed base and a top plate under the hive. Constrain lateral movement so the cell only sees vertical load.

---

## 3. Weight calibration

Calibration is stored in flash and survives power loss. Do this once per hive (repeat after mechanical changes).

### Option A — Web config portal (recommended)

1. Power the hive. If it is on home WiFi, open `http://<wifi_ip>` (see serial log `wifi_ip=...`).
2. If there is no WiFi, hold the **setup button 10 seconds** → connect phone to AP `beekpr-hive-01` (password `beekpr-setup`) → open `http://192.168.4.1`.
3. In **Weight calibration**:
   - Empty platform, keep still → **Tare (empty scale)**.
   - Place a **known weight** (e.g. 8 kg) → enter kg → **Calibrate**.
4. Live reading should match the known weight within ~0.1–0.5 kg.
5. **Save settings and reboot** if you changed other settings too.

### Option B — USB serial (bench)

Connect USB, open serial monitor **115200 baud**:

```
tare          # empty platform, wait for OK
cal 8         # replace 8 with your known weight in kg
show          # verify offset and scale
```

**Tips**

- Tare and calibrate each take ~2.5 s (median of 20 samples). Keep the platform still; unstable readings fail on purpose.
- On a **scheduled wake**, the device waits **2 minutes** after boot before measuring (thermal settling). Bench mode or a prior long awake session skips this.
- `stable_kg` in telemetry is a median of recent samples — use it for graphs and alerts.
- Slow drift over hours (±50 g) is normal load-cell creep and temperature — track trends, not absolute grams. Use `temp_scale_c` (DS18B20 on the frame) to correlate drift with temperature.
- NAU7802: **3.3 V** supply, SDA on **GPIO 19**, SCL on **GPIO 18** (I2C address 0x2A).
- This is for **hive monitoring**, not certified trade weighing.

---

## 4. Connectivity modes

| Mode | When | MQTT broker | TLS |
|------|------|-------------|-----|
| **WiFi** | Hive at home on LAN | Broker LAN IP, port **1883** | Off |
| **GSM** | Remote apiary | Public static IP, port **8883** | On |

Set in the config portal (**Operating mode** + **MQTT broker** section) or serial:

```
setwificred MyHomeNet mypassword
setmode wifi
reboot
```

**MQTT broker fields (portal or NVS)** — same for both modes:

| Field | At home | In the field |
|-------|---------|--------------|
| Host | `192.168.x.x` (HA/Mosquitto LAN IP) | Your public static IP |
| Port | `1883` | `8883` |
| Use TLS | unchecked | checked |

Factory defaults come from `.env` at build time; runtime values in the portal override them.

### Config portal access

| Situation | How to open |
|-----------|-------------|
| Hive on home WiFi | `http://<device-ip>` anytime (passive portal) |
| Change WiFi / field setup | Hold setup button **10 s** → AP `beekpr-hive-01` → `http://192.168.4.1` |
| USB bench | Serial command `portal` (same as 10 s hold — forces AP) |

Portal sections: calibration, WiFi, GSM/APN, operating mode, MQTT broker, firmware OTA, save & reboot.

### Normal operation (power)

- **GSM:** modem and WiFi off between reports; wakes for warm-up → measure → publish → sleep.
- **WiFi:** STA may stay up at home during bench mode; scheduled cycles still deep-sleep the ESP32.
- **Scheduled publish:** **2-minute sensor warm-up** after cold boot before the weight is read (skipped if already awake ≥2 min).
- **Setup button short press** after deep sleep wakes the device for a **5-minute bench window** (serial + portal). Serial commands extend the window.
- Serial `sleep` forces deep sleep immediately; `send` runs one publish cycle now.

### Escape hatch (GSM boot)

A GSM cold boot (power connect, reset, reflash) starts a headless publish cycle that blocks for several minutes. The device prints:

```
Publish cycle starts in 5s — press setup button or hit Enter for bench mode
```

- **Within 5 s:** press the setup button or send any serial byte → the publish cycle is skipped and the device enters bench mode. Use this to fix wrong settings (e.g. bad MQTT host) that would otherwise make every boot hang in connect timeouts.
- **During the publish cycle** (network wait, MQTT connect, retry backoff): press the setup button or send serial → aborts and enters bench mode. Each TCP connect attempt takes up to **15 s**; the button is checked between attempts.
- **During retry backoff** (`Publish failed — retry in 30s`): same — button or serial aborts and enters bench mode.

### Report interval

Default **6 hours** (4× per day). Change in portal **Report interval** or serial:

```
setint 360    # minutes (1..1440)
```

Stored in flash as `tx_interval_sec` in the MQTT payload.

---

## 5. MQTT setup

### Topics (per hive)

```
beekpr/{device_id}/state          → JSON telemetry
beekpr/{device_id}/availability   → online | offline (retained)
```

Example `device_id`: `hive-01` → topics `beekpr/hive-01/state` and `beekpr/hive-01/availability`.

### JSON payload

```json
{
  "device_id": "hive-01",
  "weight_kg": 47.32,
  "stable_kg": 47.29,
  "temp_scale_c": 18.75,
  "battery_v": 3.87,
  "battery_pct": 78,
  "rssi": 26,
  "wifi_connected": true,
  "wifi_hostname": "beekpr-hive-01",
  "wifi_ip": "192.168.1.42",
  "wifi_rssi": -58,
  "tx_interval_sec": 21600,
  "cell_mcc": 255,
  "cell_mnc": 3,
  "cell_lac": 27001,
  "cell_cid": 15833
}
```

| Field | Meaning |
|-------|---------|
| `weight_kg` | Instant weight |
| `stable_kg` | Median-filtered weight (best for graphs) |
| `temp_scale_c` | Scale-frame temperature from DS18B20 (`null` if sensor missing) |
| `battery_v` / `battery_pct` | Battery status |
| `rssi` | GSM signal 0–31 (`-1` when not on GSM) |
| `wifi_*` | LAN status when in WiFi mode |
| `cell_*` | Last known cell tower IDs |
| `tx_interval_sec` | Seconds between scheduled reports |

### Broker checklist

**Home (Mosquitto on Home Assistant)**

1. Mosquitto add-on running on `127.0.0.1:1883` for HA.
2. TLS listener on **8883** for field devices (see [`mqtt-tls-setup.md`](mqtt-tls-setup.md)).
3. Per-hive MQTT user, e.g. `hive01` / strong password in add-on `logins`.
4. Router: forward **8883/TCP only** to the HA host — **never 1883**.
5. Embed CA in firmware: `cp ~/beekpr-certs/ca.crt certs/ca.pem` before build.

**Device credentials**

Match Mosquitto `logins` in `.env` (build-time) or ensure they were saved via portal:

```
MQTT_USERNAME=hive01
MQTT_PASSWORD=...
MQTT_BROKER_HOST=203.0.113.1    # public IP for GSM
MQTT_BROKER_PORT=8883
MQTT_USE_TLS=1
```

For WiFi at home, set host to LAN IP, port `1883`, TLS off in the portal.

### Verify MQTT before Home Assistant

On the HA host (local broker):

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -u mqtt -P 'YOUR_HA_MQTT_PASS' -t 'beekpr/#' -v
```

Trigger a publish from the hive (serial `send` or `mqtt`, or wait for the scheduled wake).

You should see:

```
beekpr/hive-01/state {"device_id":"hive-01","weight_kg":...}
beekpr/hive-01/availability online
```

---

## 6. Home Assistant integration

All sensors are grouped under **one device** (“Hive 01”) using shared `device.identifiers` in the MQTT config.

### Step 1 — MQTT integration

1. **Settings → Devices & services → MQTT**.
2. If not configured: broker `127.0.0.1`, port `1883`, username/password for your HA MQTT user (often user `mqtt`).

### Step 2 — Install entity package

1. Copy from this repo:

   ```bash
   scp doc/home-assistant/mqtt_sensors.yaml root@HOMEASSISTANT:/config/packages/beekpr_hive_01.yaml
   ```

2. Enable packages in `configuration.yaml` if needed:

   ```yaml
   homeassistant:
     packages: !include_dir_named packages/
   ```

3. If your `DEVICE_ID` is not `hive-01`, edit the YAML: replace `hive-01` in topics and `hive_01` in `unique_id` / `identifiers`.

4. **Developer tools → YAML → Restart** (or restart Home Assistant).

### Step 3 — Confirm device

After the hive publishes once:

1. **Settings → Devices & services → MQTT → Devices**.
2. Open **Hive 01** — you should see all entities on one card:

| Entity | Type | Use |
|--------|------|-----|
| Hive 01 Weight | sensor (kg) | Live weight |
| Hive 01 Weight (stable) | sensor (kg) | Graphs & automations |
| Hive 01 Battery | sensor (%) | Charge level |
| Hive 01 Battery voltage | sensor (V) | Diagnostics |
| Hive 01 GSM signal | sensor | Field signal quality |
| Hive 01 WiFi connected | binary_sensor | LAN mode indicator |
| Hive 01 WiFi hostname / IP / signal | sensors | Bench & home debugging |
| Hive 01 Cell MCC/MNC/LAC/CID | sensors | Tower location |
| Hive 01 Report interval | sensor (s) | Expected wake period |
| Hive 01 Online | binary_sensor | `online` / `offline` topic |

**Offline detection:** sensors use `expire_after: 45000` (12.5 h). Set this to **2× your report interval** in seconds if you use a shorter `setint` (e.g. `7200` for 1 h interval).

### Step 4 — Dashboard card (Lovelace)

Add a **Entities** or **Gauge** card:

```yaml
type: entities
title: Hive 01
entities:
  - entity: sensor.hive_01_weight_stable
    name: Weight
  - entity: sensor.hive_01_battery
  - entity: binary_sensor.hive_01_online
    name: Online
  - entity: sensor.hive_01_gsm_rssi
    name: GSM signal
show_header_toggle: false
```

Or a weight history chart: **History graph** card on `sensor.hive_01_weight_stable`.

### Step 5 — Optional automations

Examples (commented) in [`home-assistant/automations.yaml`](home-assistant/automations.yaml):

- Low battery (`sensor.hive_01_battery` below 20%)
- Hive offline (`binary_sensor.hive_01_online` off for 5 minutes)
- Rapid weight drop (template — tune threshold for your hive)

Create via **Settings → Automations** or paste into `automations.yaml`.

---

## 7. Day-to-day use

### At home (WiFi mode)

1. Configure WiFi + LAN broker in portal (TLS off, port 1883).
2. Mount hive on the scale; calibrate once.
3. Device reports every `setint` interval; view weight in HA.
4. To change WiFi or move hive: hold button 10 s → AP portal → update → save & reboot.

### In the field (GSM mode)

1. `setmode gsm` (or portal), MQTT public IP + port 8883 + TLS on.
2. Ensure SIM has data, antenna mounted, port forward 8883 active.
3. Device wakes, connects GPRS, publishes, sleeps — no interaction needed.
4. Check HA for weight trend and `binary_sensor.hive_01_online`.

### USB maintenance (apiary visit)

1. **GSM field mode:** cold boot publishes once and sleeps. To get an interactive session instead, use the **5-second escape window** at boot (press setup button or hit Enter), or press the setup button after the device is asleep.
2. Serial monitor 115200 — live `weight_kg` / `mqtt_payload` lines.
3. Commands: `show`, `send`, `portal`, `setint`, `reboot`, `sleep`.
4. After **5 minutes** without serial input, device publishes once and sleeps (unless AP portal is open).

### What to expect in Home Assistant

- Weight updates **only when the hive wakes** (not continuous).
- `stable_kg` changes slowly — normal for median filtering.
- Between wakes, entities may show **unavailable** after `expire_after` — that is expected.
- `availability` topic shows `online` after each successful publish (retained).

---

## 8. Troubleshooting

| Symptom | Check |
|---------|--------|
| `raw=not_ready` | NAU7802 wiring, 3.3 V supply, SDA=19 SCL=18; check boot log for `ERR NAU7802 not found` |
| `temp_scale_c=unavailable` | DS18B20 wiring (DQ=25) or missing 4.7 kΩ pull-up |
| Weight drifts / wrong sign | Re-tare; swap A+/A− if inverted; expect ±50 g/h thermal creep on bench |
| Weight jumps after sleep | Fixed in firmware (ADC warm-up + 2 min thermal delay before publish) |
| No MQTT in HA | `mosquitto_sub` on broker; hive `send` or `mqtt` command |
| GSM connect OK, no data in HA | Fixed in firmware — ensure latest build (modem TX drain) |
| TLS fails on `mqtt` | `certs/ca.pem`, SAN = public IP, 8883 open, correct password |
| Portal won't open on WiFi | Hold button 10 s for **AP mode**; or `portal` on serial |
| HA entities missing | Package installed, HA restarted, topic `beekpr/hive-01/state` received |
| Device “offline” too soon | Lower `expire_after` in YAML or increase `setint` |
| Brownout on AP connect | Power from charged battery; IP5306 boost enabled in firmware |
| Stuck retrying a wrong broker (GSM) | Reset/reflash, then use the **5 s escape window** (button or Enter) → bench mode → fix host via `portal` |

---

## 9. Quick reference

### Serial commands

```
tare | cal <kg> | show | reset | setint <min> | setcell <mcc> <mnc> <lac> <cid>
setmode gsm|wifi | setwificred <ssid> <pass> | wificonn
modem | gprs | mqttls | mqtt | send | sleep | modemoff | portal | reboot
```

### Files in this repo

| Path | Purpose |
|------|---------|
| `doc/user-guide.md` | This guide |
| `doc/home-assistant/mqtt_sensors.yaml` | HA entities → one device |
| `doc/mqtt-tls-setup.md` | Certificates & Mosquitto TLS |
| `doc/local-setup.md` | Developer build & bench test |
| `.env.example` | Firmware secrets template |

### Multi-hive

Duplicate `mqtt_sensors.yaml` per hive (`beekpr_hive_02.yaml`), replace `hive-01` → `hive-02` in topics and `unique_id` / `identifiers`. Each hive gets its own MQTT user on Mosquitto.
