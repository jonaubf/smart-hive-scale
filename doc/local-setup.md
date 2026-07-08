# Local development setup

Instructions for building and flashing Smart Hive Scale firmware on your machine.

## Prerequisites

- **TTGO T-Call V1.3** board (USB data cable — not charge-only)
- **macOS / Linux / Windows** with USB port
- **Python 3** (for project virtualenv)
- **Git**

macOS note: the board uses a **CP2102** USB-serial chip. Install the [Silicon Labs CP210x driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) if the serial port does not appear.

## One-time setup

Clone the repository and enter the project directory:

```bash
git clone <your-repo-url> beekpr-weights
cd beekpr-weights
```

Create a local secrets file:

```bash
cp .env.example .env
```

Edit `.env` with real MQTT credentials before builds that need them. Step 2 (HX711 bench test) does not use MQTT yet.

### PlatformIO in a virtualenv (recommended)

Install PlatformIO inside a project-local `.venv` (not committed to git):

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -U pip platformio
```

Verify:

```bash
.venv/bin/pio --version
```

Optional — activate the venv for the current shell so you can type `pio` directly:

```bash
source .venv/bin/activate   # macOS / Linux
pio --version
deactivate                  # when done
```

On Windows (PowerShell):

```powershell
.venv\Scripts\Activate.ps1
pio --version
```

## Build, upload, monitor

From the project root, using the venv binary (works without activating):

```bash
.venv/bin/pio run                  # compile
.venv/bin/pio run -t upload        # flash connected board
.venv/bin/pio device monitor -b 115200
```

Or, with the venv activated:

```bash
pio run
pio run -t upload
pio device monitor -b 115200
```

List detected serial ports:

```bash
.venv/bin/pio device list
```

Force a specific port:

```bash
.venv/bin/pio run -t upload --upload-port /dev/cu.SLAB_USBtoUART
```

## Option — Cursor / VS Code PlatformIO extension

You can also install the [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension. It bundles its own PlatformIO Core and does not require the `.venv` above.

If you use both the extension and the venv CLI, prefer **one** for uploads to avoid port conflicts.

Extension workflow:

1. Open the project folder in Cursor / VS Code
2. Select environment **`ttgo-t-call`**
3. **Build** → **Upload** → **Monitor** (115200 baud) from the PlatformIO sidebar

## Wiring

![Wiring diagram: TTGO T-Call V1.3, HX711, load cell, setup button](tcall_hx711_wiring.svg)

### TTGO T-Call V1.3 pinout

Onboard SIM800L and reserved pins are marked on the diagram. **Do not use GPIO 21/22** (IP5306 I2C) or modem pins **4, 5, 23, 26, 27** for external sensors.

![TTGO T-Call V1.3 pinout](T-Call.jpg)

Official board repo: [LilyGO T-Call SIM800](https://github.com/xinyuan-lilygo/lilygo-t-call-sim800)

### HX711 and load cell

| HX711 | ESP32 (TTGO T-Call) |
|-------|---------------------|
| VCC   | 3.3 V               |
| GND   | GND                 |
| DT    | GPIO 14             |
| SCK   | GPIO 12             |

| Load cell wire | HX711 | Notes (this project's cell) |
|----------------|-------|-----------------------------|
| White          | E+    | `vref` on original scale PCB |
| Black          | E−    | `gnd` on original scale PCB |
| Green          | A+    | `nn` — swap with Red if sign is wrong |
| Red            | A−    | `np` |

### Setup button (config portal)

Wire a **normally-open (NO)** push button between **GPIO 13** and **GND**. The firmware uses the internal pull-up; no external resistor needed.

| Button terminal | ESP32 |
|-----------------|-------|
| One side        | GPIO 13 |
| Other side      | GND   |

**Hold the button for 10 seconds** to open the WiFi config portal (soft-AP). Release earlier to cancel.

## Calibration (Step 3)

Serial monitor at **115200 baud**. Commands:

| Command | Action |
|---------|--------|
| `tare` | Zero the scale (empty platform) |
| `cal 8` | Calibrate with known weight on the cell (use exact kg) |
| `show` | Print stored offset and scale |
| `reset` | Clear calibration from flash |
| `setint 360` | Set telemetry interval to 360 minutes (stored in flash) |
| `setcell 255 255 1234 56789` | Set cell tower IDs manually (normally filled by `modem` command) |
| `modem` | Power on SIM800L, register on network, print RSSI/operator/cell IDs |
| `modemoff` | Power off the modem |
| `gprs` | Full GPRS test: register → attach GPRS → TCP to MQTT broker → disconnect |
| `mqtt` | Full MQTT test: GPRS → TLS → publish state + availability to Mosquitto |
| `setmode gsm` | Use cellular GPRS for MQTT (default; reboot to apply) |
| `setmode wifi` | Use home WiFi STA for MQTT (needs `setwificred` first) |
| `setwificred MySSID mypass` | Store WiFi credentials in NVS |
| `portal` | Open config portal immediately (same as 10s button hold) |
| `wificonn` | Connect to saved WiFi (when `setmode wifi`) and print status |
| `reboot` | Restart the device |

### Config portal (button or `portal` command)

Hold the setup button **10 seconds**, or send `portal` on serial. The device starts a WiFi access point:

- **SSID:** `beekpr-hive-01` (uses your `DEVICE_ID`)
- **Password:** `beekpr-setup` (override via `WIFI_AP_PASSWORD` in `.env`)
- **URL:** `http://192.168.4.1`

The web page lets you:

1. **Calibrate** — live weight, tare, calibrate with known kg
2. **WiFi client** — SSID and password for home LAN mode
3. **GSM / SIM** — APN, username, password (for SIM changes)
4. **Operating mode** — GSM or WiFi
5. **Firmware update** — upload `.bin` file
6. **Save settings and reboot** — writes all settings to flash and restarts

**How to open the page:**

| Situation | URL |
|-----------|-----|
| Hive on home WiFi (`setmode wifi`) | `http://<device-ip>` — e.g. `http://192.168.68.134` (see serial `wifi_ip`) |
| Field / no WiFi (button 10s or `portal` in GSM mode) | Connect to AP `beekpr-hive-01`, open `http://192.168.4.1` |

In WiFi client mode the config server starts automatically after connect. It stays on your LAN IP and does **not** switch to AP mode.

Forms are **prefilled** with values already stored on the device. After save or OTA, the device reboots into normal **gsm** or **wifi** mode (portal is session-only, not persisted).

### Operating modes (persisted)

| Mode | Use case | Radios |
|------|----------|--------|
| **gsm** (default) | Field hives, no WiFi | WiFi/BT off; modem on only during TX |
| **wifi** | Hive at home on LAN | WiFi STA on during TX; modem off |

On normal operation, WiFi and Bluetooth are **explicitly disabled** to save power.

### WiFi client mode (`setmode wifi`)

1. Set credentials in config portal or via `setwificred MySSID mypass`
2. `setmode wifi` then `reboot` (or `wificonn` on the bench without reboot)
3. Device hostname on LAN: **`beekpr-hive-01`** (from `DEVICE_ID`)
4. Check connection on serial with `show` or in each reading line:

```
wifi_connected=yes wifi_ip=192.168.1.42 wifi_rssi=-58
```

MQTT payload also includes `wifi_connected`, `wifi_hostname`, `wifi_ip`, `wifi_rssi`.

**Firmware file for OTA:** `.pio/build/ttgo-t-call/firmware.bin`

**Procedure (trade-scale mechanics):**

1. Power on, wait **~60 s**
2. Empty platform, keep still → `tare`  
   - 3 s settle, 5 readings (2 s apart)  
   - Fails if spread > 500 counts (unstable)
3. Place known weight, keep still → `cal 8` (your actual kg)  
   - 2 s settle, 7 readings (1 s apart) — shorter window reduces creep while loaded  
   - Fails if samples are too noisy
4. Readings show `weight_kg` (instant), `stable_kg` (median of last 5), and `mqtt_payload=...`

**Persistence:** stored in NVS flash (survives power off). `reset` clears it.

**Note:** Replacing the scale’s original electronics with ESP32+HX711 voids trade certification. Use for hive monitoring, not legal sale weighing.

## Modem test (Step 4)

Requires a **2G SIM** with data plan inserted (nano SIM). Antenna connected.

1. Ensure `setmode gsm` (default) — modem is not used in WiFi mode
2. Optional: set `GSM_PIN=` in `.env` if your SIM has a PIN
3. Flash firmware and open serial monitor at **115200**
4. Run:

```bash
modem
```

Expected output (after up to ~2 min):

```
Modem power on
Modem initializing...
modem_info=...
sim_status=ready
imei=...
Waiting for network (120 s max)...
OK network registered
modem_powered=yes
network_registered=yes
rssi=21
operator=Kyivstar
cell_mcc=255 cell_mnc=3 cell_lac=... cell_cid=...
```

5. Power down when done: `modemoff`

Cell tower IDs are saved to NVS and appear in `mqtt_payload` as `cell_*` and `rssi` (GSM signal 0–31).

## GPRS connection test (Step 5)

Requires Step 4 working (`modem` registers on network). Set `MQTT_BROKER_HOST` and `MQTT_BROKER_PORT` in `.env` (default port **8883** for TLS).

```bash
gprs
```

This runs end-to-end:

1. Power on / register on 2G network (if not already)
2. Attach GPRS with APN from NVS (`gsm_apn`, default `internet`)
3. Open a **plain TCP** socket to `MQTT_BROKER_HOST:MQTT_BROKER_PORT`
4. Close socket and disconnect GPRS

Expected output:

```
GPRS connecting apn=internet user=(empty)...
OK GPRS connected ip=10.x.x.x
TCP connect 203.0.113.1:8883 (timeout 30s)...
OK TCP connected
TCP closed
GPRS disconnected
gprs_connected=no
mqtt_broker=203.0.113.1:8883
```

**Note:** Step 5 tests TCP reachability only (no TLS/MQTT yet — that's Step 6). If TCP fails, check: SIM data plan active, correct public IP in `.env`, router port forward **8883** configured.

## MQTT publish test (Step 6)

Requires Step 5 working (`gprs` reaches broker TCP). Embed your CA certificate before building:

```bash
cp ~/beekpr-certs/ca.crt certs/ca.pem
```

Rebuild and flash so `certs/ca.pem` is compiled into firmware. `.env` must have `MQTT_BROKER_HOST`, `MQTT_BROKER_PORT=8883`, `MQTT_USE_TLS=1`, and MQTT credentials matching Mosquitto logins.

```bash
mqtt
```

This runs end-to-end:

1. Power on / register on 2G network (if not already)
2. Attach GPRS with APN from NVS
3. Upload CA certificate to the SIM800L modem (once per boot)
4. TLS connect to `MQTT_BROKER_HOST:8883`
5. Publish JSON to `beekpr/{device_id}/state` and `online` to `beekpr/{device_id}/availability`
6. Disconnect MQTT and GPRS

TLS is performed on the **ESP32** (TLS 1.2), not the SIM800 modem — required because modem SSL is too old for modern Mosquitto.

Expected output:

```
GPRS connecting apn=internet user=(empty)...
OK GPRS connected ip=10.x.x.x
MQTT connect 203.0.113.1:8883 tls=1 user=hive-01 (ESP32 TLS)...
OK MQTT connected
MQTT publish beekpr/hive-01/state len=...
OK MQTT published
MQTT publish beekpr/hive-01/availability len=6
OK MQTT published
MQTT disconnected
GPRS disconnected
```

Verify on your broker host:

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -u homeassistant -P '...' -t 'beekpr/#' -v
```

If MQTT TLS fails but `gprs` TCP works, check: `certs/ca.pem` present at build time, server cert SAN includes your public IP, Mosquitto TLS listener on 8883, correct MQTT username/password.

**HA Mosquitto add-on:** startup log must show `Opening ipv4 listen socket on port 8883`. If it says `SSL is not enabled`, TLS is off — see [`doc/mqtt-tls-setup.md`](mqtt-tls-setup.md) section 2.

Serial command `mqttls` tests TLS socket only (no MQTT); use it before `mqtt` when debugging.

## Expected serial output

Calibrated:

```
raw=108390 weight_kg=0.012 stable_kg=0.003
raw=352426 weight_kg=8.011 stable_kg=8.008
```

Uncalibrated:

```
raw=108390 weight_kg=uncalibrated
```

Without HX711 connected:

```
raw=not_ready
```

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `pio: command not found` | Use `.venv/bin/pio` or run `source .venv/bin/activate` |
| `.venv` missing | Run the one-time venv setup commands above |
| Upload fails / no port | Install CP210x driver; try another USB cable |
| `WARNING: .env not found` | Run `cp .env.example .env` |
| `raw=not_ready` always | Check 3.3 V, GND, DT/SCK pins; verify HX711 LED/power |
| Build downloads fail | Check internet; retry `.venv/bin/pio run` |
