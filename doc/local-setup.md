# Local development setup

Instructions for building and flashing Smart Hive Scale firmware on your machine.

Targets the discrete **ESP32-WROOM-32** build: standalone SIM800L, TP4056 + 2× CR-SJ5530 power chain, 4 corner
load cells (each behind its own NAU7802) on a PCA9548A I2C mux, DS3231 precision RTC — see
[spec.md](../spec.md) §10 for the full wiring/GPIO map and BOM. For the retired TTGO T-Call firmware, use the
`tcall-v1` git tag.

## Prerequisites

- **ESP32-WROOM-32** dev board, wired per spec.md §10 (USB data cable — not charge-only)
- A charged Li-Ion/LiPo battery connected to the TP4056 — **required even on the bench**. The TP4056 alone
  cannot source the SIM800L's ~2A boot-current bursts; without a battery to buffer them, the whole supply
  oscillates and resets the board. USB can stay attached too (it only charges the battery through the TP4056),
  but never run the board on USB with no battery fitted.
- **macOS / Linux / Windows** with USB port
- **Python 3** (for project virtualenv)
- **Git**

macOS note: most ESP32-WROOM-32 devkits use a **CP2102** or **CH340** USB-serial chip. Install the
[Silicon Labs CP210x driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers) (CP2102) or the
appropriate CH340 driver if the serial port does not appear.

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

Edit `.env` with real MQTT credentials before builds that need them. Bench weight testing does not need MQTT.

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
.venv/bin/pio run -t upload --upload-port /dev/cu.usbserial-XXXX
```

## Option — Cursor / VS Code PlatformIO extension

You can also install the [PlatformIO IDE](https://platformio.org/install/ide?install=vscode) extension. It bundles its own PlatformIO Core and does not require the `.venv` above.

If you use both the extension and the venv CLI, prefer **one** for uploads to avoid port conflicts.

Extension workflow:

1. Open the project folder in Cursor / VS Code
2. Select environment **`esp32-discrete`**
3. **Build** → **Upload** → **Monitor** (115200 baud) from the PlatformIO sidebar

## Wiring

![Wiring diagram: ESP32-WROOM-32, 4× NAU7802 + PCA9548A, DS3231, DS18B20, SIM800L, power chain](smart-apiary-scale-schematic.svg)

Full BOM, mechanical notes, and power-chain wiring: [spec.md §5/§10](../spec.md#10-hardware-connections).
SIM800L-specific wiring/bring-up: [`esp32_sim800l.md`](esp32_sim800l.md).

### Board prep before assembly — remove every indicator LED

**Required, not optional** (spec.md §5 "Indicator LEDs must be removed"): desolder the stock power LED (or
its series resistor) from the **ESP32 devkit**, **all 4 NAU7802 boards**, the **DS3231 module** (plus its
CR2032 charging circuit on ZS-042-style boards), and **both CR-SJ5530s** (with their `PS`-pad shorts). They
all sit on the always-on 3.3 V rail, outside any firmware control, and together burn several mA through
every deep sleep — an order of magnitude over the whole < 300 µA sleep budget.

Rework with the board unpowered, and **verify each board still responds (`i2cscan`) before doing the
next** — on cramped modules the LED trace can run through to the chip's own VCC (this bit the first unit's
DS3231: a damaged trace left its VCC floating at ~2 V and took the whole shared bus down intermittently,
needing a bodge wire). Finish with the sleep-current audit below.

### ESP32-WROOM-32 GPIO map

Bare devkit — no onboard modem/PMIC, so the only pins to avoid are the universal ESP32 ones (strapping 0/2/12/15,
UART0 1/3, internal flash 6-11) plus what this project itself reserves:

| Signal | GPIO | Notes |
|--------|------|-------|
| Modem UART2 TX (ESP → SIM800L RXD) | 17 | Via 1k series + 5.6k shunt divider (3.3V → ~2.8V) |
| Modem UART2 RX (ESP ← SIM800L TXD) | 16 | Via 1k series (protection only) |
| Modem rail enable (`EN`) | 4 | Drives CR-SJ5530 #2's `EN` — this SIM800L board has no `PWRKEY` pin |
| Modem RST | 5 | Active low; emergency reset only |
| I2C SDA / SCL (single shared bus) | 21 / 22 | PCA9548A mux, DS3231, and all 4 NAU7802 (behind the mux) |
| DS18B20 OneWire | 25 | 4.7 kΩ pull-up to 3.3 V required |
| DS3231 SQW/INT (ext1 wake) | 14 | |
| Setup button (ext0 wake) | 13 | NO to GND, internal pull-up |
| Battery ADC | 35 | ADC1, input-only — 100k/100k divider, calibrate per board (see below) |

### I2C bus

One shared bus (GPIO 21/22) carries everything:

| Device | I2C address | Position |
|--------|-------------|----------|
| PCA9548A | `0x70` | Upstream, directly on the ESP32's bus |
| DS3231 | `0x68` | Upstream — unique address, no mux isolation needed |
| NAU7802 × 4 | `0x2A` each (fixed) | Behind mux channels **0, 1, 2, 7** — one per corner (`CORNER_MUX_CHANNEL` in `config.h`) |

Use serial `i2cscan` to verify: it scans the main bus (expect `0x70` + `0x68`), then probes each corner's mux
channel for its NAU7802.

### NAU7802 and load cell (×4, one per corner)

| NAU7802 | Connects to |
|---------|-------------|
| VIN | 3.3 V rail |
| GND | GND |
| SCL | PCA9548A channel N (`SCn`) — N ∈ {0, 1, 2, 7} |
| SDA | PCA9548A channel N (`SDn`) |

Firmware runs each chip at I2C **0x2A**, gain 128, 10 SPS, and puts it into register-controlled power-down
before deep sleep. Note this only quiesces the chip's own ADC/analog front end — it does **not** cut its power
rail (that rail is always-on; see the "sleep current" note below).

| Load cell wire | NAU7802 | Notes |
|-----------------|---------|-------|
| Red | E+ | Excitation + |
| Black | E− | Excitation − |
| Green | A+ | Signal + |
| White | A− | Signal − |
| Bare shield | GND | Single-ended — tie at one end only |

Bridge excitation comes from the NAU7802's internal LDO (3.0 V, set by firmware) — the load cell needs no
separate supply.

### DS18B20 scale temperature sensor

| DS18B20 | ESP32 |
|---------|-------|
| VDD     | 3.3 V |
| GND     | GND   |
| DQ      | GPIO 25 |

**Required:** a **4.7 kΩ** resistor from DQ to 3.3 V (OneWire pull-up). Mount the probe on the scale frame near
a load cell — its reading is published as `temp_scale_c`. If the sensor is missing, firmware logs an error at
boot and publishes `temp_scale_c: null`.

### Setup button (config portal)

Wire a **normally-open (NO)** push button between **GPIO 13** and **GND**. Firmware uses the internal pull-up;
no external resistor needed. **Hold 10 seconds** to open the WiFi config portal (soft-AP). Release earlier to
cancel.

### DS3231 precision RTC (report scheduling)

| DS3231 | ESP32 |
|--------|-------|
| VCC | 3.3 V |
| GND | GND |
| SDA | GPIO 21 (shared bus) |
| SCL | GPIO 22 (shared bus) |
| SQW/INT | GPIO 14 |

I2C address **0x68**. `SQW`/`INT` is open-drain (the module has its own pull-up); it wakes the ESP32 from deep
sleep via `ext1` when the scheduled report alarm fires, so reports land on precise wall-clock time instead of
drifting on the ESP32's own RC-oscillator timer over weeks of deep sleep. If it isn't detected at boot, firmware
logs an error and falls back to the ESP32's internal timer for scheduling (still correct, just less precise —
see [CLAUDE.md](../CLAUDE.md)). Fit a CR2032 in the module's holder — the DS3231 keeps its own time and alarm
state through a full power loss as long as the coin cell is good, which the ESP32's own RTC memory cannot do.

**A brand-new/never-set module** will show `lost_power=yes` and a bogus power-on-reset date on `show` — this
does **not** break scheduling (alarms match on hour:minute only), but it's cosmetically wrong until synced. In
**GSM mode**, firmware syncs the DS3231 from the ESP32's system clock automatically once it's plausible (after
`modemManagerSyncClock()`'s NITZ/NTP-over-GPRS sync succeeds during the first successful publish) — no action
needed, just let one publish cycle complete. **WiFi mode has no clock source yet**, so the DS3231 won't
self-correct there until one is added.

### Battery voltage divider — calibrate per board

`BATTERY_DIVIDER_RATIO` in `config.h` is nominal (100kΩ/100kΩ ≈ 2:1); real resistor tolerances shift it a few
percent per board. Calibrate once per board:

```
battery
```

prints the firmware's reading, current battery %, and the ratio in effect. Compare against a multimeter reading
on the raw battery node (Battery+/GND, **USB/charger disconnected** so the reading isn't skewed by charge
current) and compute:

```
new_ratio = old_ratio * V_multimeter / V_reported
```

Set the result as `BATTERY_DIVIDER_RATIO` in `config.h` and reflash.

## Calibration

Serial monitor at **115200 baud**. Commands:

| Command | Action |
|---------|--------|
| `tare` | Zero all 4 corners in one pass (empty platform); each corner gets its own offset |
| `cal 8` | Derive the shared span factor from a known weight (kg) summed across all 4 tared corners |
| `show` | Print stored per-corner offsets, shared span, and calibration status |
| `reset` | Clear calibration from flash |
| `setint 360` | Set telemetry interval to 360 minutes (stored in flash) |
| `setcell 255 255 1234 56789` | Set cell tower IDs manually (normally filled by `modem`) |
| `modem` | Power on SIM800L, register on network, print RSSI/operator/cell IDs |
| `modemoff` | Power off the modem (rail cut via `EN`) |
| `battery` | Print battery voltage/%/divider ratio — for per-board divider calibration |
| `settime 2026-08-16 07:30:00` | Set the DS3231 manually, **in UTC** — recovery when NITZ/NTP sync never succeeds |
| `i2cscan` | Scan the shared bus (expect `0x70` PCA9548A + `0x68` DS3231), then each mux channel for its NAU7802 |
| `gprs` | Full GPRS test: register → attach GPRS → TCP to MQTT broker → disconnect |
| `mqtt` | Full MQTT test: GPRS → TLS → publish state + availability to Mosquitto |
| `setmode gsm` | Use cellular GPRS for MQTT (default; reboot to apply) |
| `setmode wifi` | Use home WiFi STA for MQTT (needs `setwificred` first) |
| `setwificred MySSID mypass` | Store WiFi credentials in NVS |
| `portal` | Open config portal immediately (same as 10s button hold) |
| `wificonn` | Connect to saved WiFi (when `setmode wifi`) and print status |
| `send` | Run one full publish cycle now (with retries) |
| `sleep` | Enter deep sleep immediately (wake: setup button or RTC alarm) |
| `reboot` | Restart the device |

### Bench mode and deep sleep

- **GSM mode, cold boot** (reset/reflash/power connect): the device prints `Publish cycle starts in 5s — press setup button or hit Enter for bench mode`. Do nothing → it publishes headlessly and deep-sleeps. Press the button or hit Enter within 5 s → interactive bench mode (all commands above).
- **WiFi mode, cold boot:** bench mode starts directly.
- Bench mode lasts **5 minutes**; each serial command extends it. When it expires, the device publishes once and deep-sleeps.
- From deep sleep, a **short button press** wakes into bench mode; the DS3231 alarm wakes into a headless publish cycle.
- During publish retries (`Publish failed — retry in 30s`), pressing the setup button or sending serial aborts and enters bench mode.
- During network wait and MQTT connect, button/serial aborts between TCP attempts (each attempt up to **15 s**).
- Before each scheduled publish, a **2-minute sensor warm-up** runs (thermal settling). Button/serial aborts it.
- Deep sleep: all 4 NAU7802 are put into register power-down, WiFi is fully stopped, modem rail is cut (`EN` low, latched through sleep). Setup button (GPIO 13) wakes via ext0.

### Config portal (button or `portal` command)

Hold the setup button **10 seconds**, or send `portal` on serial. The device starts a WiFi access point:

- **SSID:** `beekpr-hive-01` (uses your `DEVICE_ID`)
- **Password:** `beekpr-setup` (override via `WIFI_AP_PASSWORD` in `.env`)
- **URL:** `http://192.168.4.1`

The web page lets you:

1. **Calibrate** — live per-corner weight, tare all 4 corners, calibrate shared span with known kg
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

**Firmware file for OTA:** `.pio/build/esp32-discrete/firmware.bin`

**Procedure (mechanics):**

1. Power on with a battery connected, wait a few seconds (ADC warm-up discard on boot)
2. Empty platform, keep still → `tare`
   - Median of **20** consecutive samples per corner (~2.5 s each)
   - Fails if spread > 500 counts on any corner (unstable)
3. Place one known weight roughly centered on the platform, keep still → `cal 8` (your actual kg)
   - Firmware sums all 4 (now-tared) corners' raw readings and derives one shared scale factor from that sum
   - Fails if samples are too noisy
4. Readings show per-corner `c0=… c1=… c2=… c3=…`, `weight_kg` (instant sum), `stable_kg` (median of last 5), and `mqtt_payload=...`

This only assumes the *sum* of the 4 corners tracks weight linearly — it doesn't require the frame to
distribute load exactly 1/4 to each corner, or the 4 cells to be perfectly matched. Watch the per-corner fields
over time: a corner that drifts away from the other 3 is the first sign of a loose mount, damaged cell, or a
corner that needs re-taring.

**Scheduled reports:** before measuring, the device waits **2 minutes** after boot for thermal settling
(`Sensor warm-up: measuring in Ns` on serial). Skipped if the device has already been awake that long (e.g.
after the 5-minute bench window). Setup button or serial aborts the wait.

**Persistence:** stored in NVS flash (survives power off). `reset` clears it.

**Note:** this is for hive monitoring, not certified trade weighing.

## Modem test

Requires a **2G SIM** with data plan inserted (nano SIM), antenna connected, and — critically — **a battery
connected to the TP4056**, not USB power alone (see Prerequisites above; the modem's boot-current bursts will
brownout a USB-only supply).

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

The SIM800L's own netlight LED should start blinking within a few seconds of the rail coming up — if it never
lights and `ERR modem not answering AT after rail-up` appears, see [`esp32_sim800l.md`](esp32_sim800l.md) for
the power-chain bring-up checklist (bulk cap placement, wire gauge, battery vs. USB-only power).

5. Power down when done: `modemoff`

Cell tower IDs are saved to NVS and appear in `mqtt_payload` as `cell_*` and `rssi` (GSM signal 0–31).

## GPRS connection test

Requires the modem test working (`modem` registers on network). Set `MQTT_BROKER_HOST` and `MQTT_BROKER_PORT` in `.env` (default port **8883** for TLS).

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

**Note:** this tests TCP reachability only (no TLS/MQTT yet — that's next). If TCP fails, check: SIM data plan active, correct public IP in `.env`, router port forward **8883** configured.

## MQTT publish test

Requires the GPRS test working (`gprs` reaches broker TCP). Embed your CA certificate before building:

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

## Sleep-current audit

Before field deployment, confirm actual deep-sleep draw at the battery — budget is well under ~300 µA total
(ESP32 ~10 µA + 2× CR-SJ5530 <100 µA each + divider ~21 µA + TP4056 leakage). Put a multimeter **in series**
with the battery's + lead (start on the mA range — boot and publish draw hundreds of mA), send `sleep`, wait for
the board to go quiet, then step down to µA.

If you see milliamps instead of microamps, the most common cause on a freshly-populated board isn't firmware —
it's **hardwired indicator LEDs**. The sensor VCC rail (ESP32 `3V3` + all 4 NAU7802s + mux + DS3231 + DS18B20)
is always-on, deep sleep included (see CLAUDE.md), so any board with a stock power LED soldered straight to
that rail keeps burning current regardless of what firmware does. Check, in order:

1. Generic ESP32 devkit's own onboard power LED (often labeled `PWR`, near the USB connector)
2. Each of the 4 NAU7802 breakout boards' own power LED
3. The DS3231 module's power LED (and CR2032 "charging" circuit, if present — shouldn't be charging a
   non-rechargeable coin cell in the first place)

None of these are under firmware control — desolder the LED (or its series resistor, generally the safer
target) on each board that has one, with the board unpowered; this is the same required prep step described
under "Board prep before assembly" above, including the verify-with-`i2cscan`-after-each-board caution.
Only after that does GPIO4's rail hold (modem) and the register power-down (sensors) reflect the true sleep
budget.

## Expected serial output

Calibrated:

```
raw c0=32640 c1=-21660 c2=54900 c3=-49000 weight_kg=8.011 stable_kg=8.008
```

Uncalibrated:

```
raw c0=32640 c1=-21660 c2=54900 c3=-49000 weight_kg=uncalibrated
```

Without a corner's NAU7802 connected — `ERR NAU7802 corner N not found (mux channel M, I2C 0x2A)` at boot; that
corner reports as not-present while the others still work (a failing corner degrades to a flagged outlier, not
a dead device).

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `pio: command not found` | Use `.venv/bin/pio` or run `source .venv/bin/activate` |
| `.venv` missing | Run the one-time venv setup commands above |
| Upload fails / no port | Install CP210x/CH340 driver; try another USB cable |
| `WARNING: .env not found` | Run `cp .env.example .env` |
| Board resets/behaves oddly only on battery, fine on USB | TP4056 with no battery attached can't source the modem's boot bursts and oscillates — connect a battery (see Prerequisites) |
| `ERR NAU7802 corner N not found` | Check that corner's wiring and the mux channel in `CORNER_MUX_CHANNEL` (config.h); run `i2cscan` |
| `i2cscan` shows `0x70` missing | PCA9548A wiring/power; corner channels aren't scannable until it responds |
| One corner's raw reading ramps continuously / wraps around | Floating or broken bridge input on that corner — check E+/E−/A+/A− wiring on that load cell |
| `temp_scale_c=unavailable` | Check DS18B20 wiring (DQ=25) and the 4.7 kΩ pull-up to 3.3 V |
| `ERR modem not answering AT after rail-up` | Battery not connected (see above); check bulk cap is soldered right at SIM800L VCC/GND; check GPIO4→`EN` wiring; SIM800L netlight LED dark = rail/module problem, blinking = UART wiring problem |
| Sleep current in the mA range | See "Sleep-current audit" above — almost always an un-removed indicator LED |
| Build downloads fail | Check internet; retry `.venv/bin/pio run` |
