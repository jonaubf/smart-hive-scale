# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for **Smart Hive Scale** — a battery-powered ESP32 beehive weight monitor. Wakes on a wall-clock-aligned
RTC schedule, reads 4 corner load cells via NAU7802, and publishes JSON telemetry to Home Assistant over MQTT —
either TLS/8883 via 2G GPRS (field hives) or plain/1883 over WiFi (hive at home). Deep-sleeps between reports to
conserve battery. PlatformIO + Arduino framework, no OS.

**Hardware:** discrete build — bare **ESP32-WROOM-32**, standalone **SIM800L** (own UART/power wiring, no
integrated PMIC), **TP4056 + 2× CR-SJ5530** power chain, and **4 corner load cells** behind a **PCA9548A** I2C
mux (fanned out over physical channels **0, 1, 2, 7** — see `CORNER_MUX_CHANNEL` in `config.h`). This replaced
an earlier TTGO T-Call V1.3/V1.4 single-load-cell design (ESP32-WROVER-B + integrated SIM800L + IP5306 PMIC);
that firmware is retired and preserved only at the `tcall-v1` git tag — nothing in this repo targets it anymore.

Full requirements/architecture/BOM/wiring live in [spec.md](spec.md) — read it for anything not covered here.
End-user/operator docs are in `doc/` (see README's doc table). This file is for firmware development only.

## Build / flash / monitor

Always use the project-local venv (`.venv/bin/pio`), not a global `pio`.

```bash
.venv/bin/pio run                                    # compile
.venv/bin/pio run -t upload                           # flash
.venv/bin/pio device monitor -b 115200                 # serial monitor
.venv/bin/pio run -t upload --upload-port /dev/cu.SLAB_USBtoUART   # force port
.venv/bin/pio device list                              # list serial ports
```

There is a single build environment: `esp32-discrete` (see `platformio.ini`). No unit test suite exists — this is
embedded firmware verified on real hardware via the serial bench-mode command console (see below), not `pio test`.

For IDE/clangd code intelligence (go-to-definition through Arduino framework + library headers, which plain
grep can't resolve — e.g. `TinyGsmClient.h`, `Preferences.h`), regenerate the compile database after touching
`platformio.ini` or adding files: `.venv/bin/pio run -t compiledb`. Gitignored — machine-specific, regenerate
locally rather than trusting a stale copy.

Secrets: copy `.env.example` → `.env` (gitignored) before building. `extra_scripts/load_env.py` generates
`include/build_env.h` (gitignored) from `.env` as a pre-build step; `config.h` falls back to placeholder defaults
if a value isn't defined there. TLS builds also need `certs/ca.pem` (gitignored) — `extra_scripts/embed_ca.py`
compiles it into `include/ca_pem_embed.h` at build time (`CA_PEM_AVAILABLE 0` if missing, and MQTT TLS will fail).

## Build-time library patches (`extra_scripts/`)

Run as PlatformIO `pre:` hooks, in this order — order matters since later ones depend on generated files:
1. `load_env.py` — `.env` → `include/build_env.h`
2. `embed_ca.py` — `certs/ca.pem` → `include/ca_pem_embed.h`
3. `patch_tinygsm.py` — patches the *installed* `TinyGSM` lib in `.pio/libdeps/…` to poll the modem RX buffer
   every 50ms instead of 500ms (needed for TLS handshakes over slow 2G).
4. `patch_sslclient.py` — patches the *installed* `SSLClient` lib to do non-blocking reads and pump the modem
   during TLS I/O via the `beekprModemPumpForTls()` hook (declared `extern "C"`, defined in `modem_manager.cpp`).

These patch installed dependencies in `.pio/libdeps/`, not vendored source — if `pio` reinstalls/updates
`TinyGSM` or `SSLClient`, the patches reapply automatically on next build. If TLS-over-GSM behaves oddly, suspect
these first.

## Architecture: the wake cycle

Everything is driven from `src/main.cpp`, which dispatches on **why the ESP32 woke up**
(`app_scheduler.h` — `WakeCause`: `PowerOn`, `Timer`, `RtcAlarm`, `Button`):

- **`RtcAlarm`** (DS3231 alarm via `ext1` — the scheduled report is due): run headless —
  `appSchedulerRunPublishCycle()` then `appSchedulerEnterDeepSleep()`. Never returns.
- **`Timer`** (ESP32's own internal RTC timer wake): only ever armed in the DS3231-absent fallback — if
  `rtc_clock` can't find the DS3231 on I2C at boot, `app_scheduler` falls back to sleeping the full report
  interval on the ESP32's internal timer instead of the DS3231's `ext1` alarm (unaligned to wall-clock, but
  still correct). In that fallback, `Timer` *is* the scheduled-report signal and is handled identically to
  `RtcAlarm`. With a working DS3231, `Timer` never fires at all.
- **`PowerOn`** in GSM mode (cold boot/reflash/battery connect): 5s escape window on serial/button, then same
  headless publish-and-sleep as `RtcAlarm` (a blocking multi-minute GSM cycle otherwise locks out the console).
- **`Button`** (or WiFi-mode power-on): interactive **bench mode** — serial command console + live weight
  readings for 5 minutes (`BENCH_STAY_AWAKE_MS`), extended by any serial input. On expiry, publishes once and
  sleeps. In WiFi mode the maintenance portal also comes up automatically once connected.

Publish cycle (`appSchedulerRunPublishCycle`, in `app_scheduler.cpp`): 2-minute thermal warm-up (skipped if
already awake that long) → read all 4 NAU7802 corners + DS18B20 + battery → connect (GSM: SIM800L register →
GPRS attach → TLS MQTT :8883; WiFi: STA connect → plain MQTT :1883) → publish `state` + `availability` →
disconnect. Retries with backoff up to `PUBLISH_MAX_ATTEMPTS`; setup button or serial input aborts into bench
mode at any wait point. Sensor readings — per-corner weight, temp, battery, and the `report_time` timestamp —
are captured as a `ScaleReading`/snapshot (`mqtt_client.cpp::captureSensorSnapshot()`) *before* any modem/WiFi
activity, deliberately — GSM registration, GPRS attach, and the TLS handshake all draw current spikes (SIM800
TX bursts up to ~2A) on the same shared supply the NAU7802 bridges and the DS3231's I2C bus run from, and
sampling (or reading the RTC) mid-spike can corrupt the result. This was a real bug on the retired T-Call
firmware (fixed there 2026-07-17, carried forward as an invariant into this design from day one) — it used to
sample weight *and* read the RTC *after* connecting, and the RTC half was only caught after a field report
showed a garbled `report_time` with an out-of-range month/day/minute, decoded from a glitched I2C read.
`rtcClockNowIso8601()` also validates every field is in-calendar-range before formatting, so a future glitch
degrades to `report_time: null` instead of a malformed string. Network-status fields (rssi, cell tower, WiFi
link) are read separately, after connecting, since those only exist once actually on the network.

Before every deep sleep: all 4 NAU7802 corners → register power-down, WiFi fully stopped, modem powered off
(rail cut, GPIO hold armed), DS3231 report alarm reprogrammed (below).

### Closing an MQTT session over GSM (don't "simplify" `mqttDisconnect()`)

The SIM800 buffers TX data, and TinyGSM's socket close is `AT+CIPCLOSE=<mux>,1` — a **quick close that
discards anything the radio hasn't put on the air yet**. Anything written immediately before a close can
therefore vanish, which on a 2G uplink is routine rather than an edge case. Two places in `mqtt_client.cpp`
exist purely because of this, and both look like dead weight if you don't know why:

- `drainModemTx(MODEM_TX_DRAIN_PAYLOAD_MS)` after the publishes — without it the telemetry itself is what
  gets discarded.
- `mqttDisconnect()` **deliberately does not call `PubSubClient::disconnect()`**. That method writes the
  DISCONNECT packet and calls `stop()` in the same breath, so the packet dies in the quick close. It writes
  the 2-byte DISCONNECT itself, drains (`MODEM_TX_DRAIN_DISCONNECT_MS`), and only then closes.

Symptom when the second one regresses: Mosquitto logs `disconnected: exceeded timeout` roughly
`keepalive × 1.5` after the *last packet it received*, on cycles where every publish actually landed — the
broker is just sitting on a half-open socket waiting out its grace period. Confirmed in the field
2026-08-15: `report_time`/HA `last_changed` showed the state publish arriving ~1s after CONNACK, then
nothing for 90s. A healthy cycle now logs a bare `disconnected.` (broker received a real DISCONNECT) within
a few seconds. Note this is a *cosmetic* failure mode — no telemetry is lost either way — so don't chase it
as data loss, but don't reintroduce it either: the noise buries genuine failures in the log.

Raising `MQTT_KEEPALIVE_SEC` does **not** fix this (it only moves when the broker gives up); it's set to 60
for genuine publish headroom on a slow link, nothing more.

### RTC-driven scheduling (`rtc_clock`)

A DS3231 (I2C 0x68, upstream of the PCA9548A mux on the same shared bus GPIO 21/22) drives the report schedule
via its alarm output (`PIN_RTC_ALARM`, `ext1` deep-sleep wake) so reports land on precise wall-clock time
instead of drifting on the ESP32's own RC-oscillator timer. `appSchedulerEnterDeepSleep()` reprograms the alarm
(`rtcClockSetNextAlignedAlarm()`, Alarm2/minute precision) on every real sleep entry to fire at the *next
wall-clock boundary* `settingsTxIntervalSec()` apart — e.g. a 1h interval always fires at :00, not "1h from
whenever this cycle happened to finish." Alignment is to midnight, so intervals that evenly divide 1440 minutes
(30, 60, 360, ...) land on round clock times; others still fire on a consistent schedule, just not necessarily
round numbers. If the DS3231 isn't found at boot, `rtc_clock` logs an error and `app_scheduler` falls back to
sleeping the full interval on the ESP32's internal timer in one shot from "now" (unaligned, unchunked) — see the
`Timer` `WakeCause` note above.

A never-set DS3231 boots reporting `lost_power=true` and its power-on-reset epoch — cosmetically wrong but
harmless to scheduling (Alarm2 matches hour:minute only, not the date). Before reprogramming the alarm,
`appSchedulerEnterDeepSleep()` calls `rtcClockSyncFromSystemTimeIfNeeded()`, which writes the ESP32's system
clock into the DS3231 once that clock itself is plausible — currently only `modemManagerSyncClock()` (GSM mode,
NITZ/NTP over GPRS, called during `modemManagerEnsureGprs()`) ever sets it via `settimeofday()`. WiFi mode has
no clock source yet, so a DS3231 paired with a WiFi-only device won't self-correct. `modemManagerSyncClock()`
deliberately re-syncs from NITZ/NTP on *every* call, not just the first — it used to skip re-syncing once the
system clock looked "plausible" (past a fixed date), but since the device spends ~99% of each cycle in deep
sleep, the system clock in between real syncs is dominated by the ESP32's own uncalibrated internal RTC
oscillator, not the DS3231. Combined with `rtcClockSyncFromSystemTimeIfNeeded()` trusting that same "plausible"
clock as authoritative every cycle, skipping re-sync silently re-contaminates the DS3231 with an increasingly
stale, drifting time every sleep cycle — this was observed as a steady drift-per-cycle regression on the
retired T-Call firmware and is why every-call re-sync is load-bearing here too.

### Modem rail control and the GPIO-hold trap (battery-critical)

This SIM800L module has **no `PWRKEY` pin** — it's tied to GND internally, so the module auto-boots the instant
its rail is powered. Power control therefore happens entirely at the rail: `PIN_MODEM_EN` (GPIO4) drives
CR-SJ5530 #2's `EN` pin, and since `EN` is enabled by default with GPIO4 floating until firmware configures it,
**the modem rail comes up on every cold boot before `setup()` runs** unless GPIO4 is claimed and driven low
immediately. `main.cpp::setup()`'s very first statements do exactly that — before Serial, NVS, sensors,
anything — and this ordering must not change.

The sharper trap is **deep sleep**: the ESP32 releases normal GPIO output state on sleep entry, so a plain
`digitalWrite(4, LOW)` floats once asleep, `EN` re-enables, and the modem rail (and the SIM800L on it) runs —
and drains the battery — through the entire sleep window. GPIO4 is an RTC-domain pin, so
`app_scheduler.cpp::powerDomainsAndSleep()` latches it with `gpio_hold_en(GPIO_NUM_4)` +
`gpio_deep_sleep_hold_en()` before every sleep entry, and `main.cpp::setup()` releases the hold with
`gpio_hold_dis()` on the next wake before driving the pin again. This is the single-largest battery-life
landmine in this design — treat any unexplained overnight battery drain as "check the GPIO4 hold" first, the
same way the retired T-Call firmware's #1 suspect was its PMIC's boost-keep-on bit.

### Sensor power: chip-level sleep only, not rail-level (a real trap during bring-up)

Unlike the modem, the sensor VCC rail (CR-SJ5530 #1's 3.3 V tap — ESP32 `3V3` + all 4 NAU7802s + PCA9548A +
DS3231 + DS18B20) is **never switched off**, deep sleep included — it's the same rail that has to keep the
ESP32's own RTC memory alive, and there's no per-sensor load switch gating it. `weightSensorPowerDown()`
(called before every sleep) only clears each NAU7802's PUD/PUA bits over I2C, dropping its *own* internal
current draw to sub-µA — but that's a chip-level low-power state, not a rail-level power-off. Anything hardwired
directly to that rail — most importantly, **the stock power-indicator LEDs on generic NAU7802 breakout boards
and most ESP32 devkits** — keeps drawing current through every sleep cycle regardless of what any register
says, since it was never under I2C/GPIO control in the first place. Confirmed on the first bring-up unit
(2026-08-08): 4× NAU7802 board LEDs + the ESP32 devkit's own power LED cost several mA of pure idle waste, which
alone can burn through a multi-week battery budget in a few weeks doing nothing. These have to be desoldered (or
their series resistor pulled, or the feeding trace cut) per board — no firmware fix is possible since the LEDs
aren't wired to anything firmware touches. The rework itself carries risk: on cramped modules the LED's supply
trace can run through to the chip's own VCC — removing the DS3231 board's LED on the first unit damaged its VCC
path (chip floating at ~2 V, whole shared bus intermittently timing out) and needed a bodge wire; verify each
board with `i2cscan` after its rework. Then verify with a real sleep-current measurement in series with the
battery (see spec.md §12 plan E5) after any new board is populated, before assuming `< 300 µA` sleep budget is
met.

### Power chain bring-up notes

- **The TP4056 cannot power the board on its own — a battery must always be connected**, even on the bench.
  It's a charge controller, not a supply: its current limit (~1 A on typical modules) can't source the
  SIM800L's ~2 A boot-current bursts, and with no battery to buffer the burst its regulation loop hunts,
  producing a visible ~3–4 V oscillation on the whole 3.3 V/4.2 V chain that resets everything downstream
  (confirmed on the first bring-up unit, 2026-08-08 — board looked "dead" with USB-only power, worked
  immediately once a battery was wired to TP4056 `B+`/`B−`).
- **Avoid Quick Charge (QC2.0/3.0) USB bricks as the TP4056's input.** QC adapters decide their output voltage
  via a D+/D− handshake the TP4056 doesn't speak; a brick that doesn't see the expected negotiation can default
  to boosting past 5 V, which is out of spec for a plain linear charger IC. Use a dumb, fixed-5V USB adapter.
- **`BATTERY_DIVIDER_RATIO` (`config.h`) is per-board and must be calibrated**, not trusted at its nominal
  value — real resistor tolerances shift the effective divider ratio a few percent from nominal. Bench command
  `battery` prints the firmware's current reading and the formula; compare against a multimeter on the raw
  battery node (USB/charger disconnected) and solve `new_ratio = old_ratio * V_multimeter / V_reported`.

## Module map (`src/` + `include/`, one .cpp/.h pair each)

| Module | Responsibility |
|---|---|
| `app_scheduler` | Orchestrates the wake cycle above; owns deep sleep / retry logic |
| `main.cpp` | `setup()`/`loop()`, serial command dispatch, bench-mode state (no header) |
| `config.h` | All tunable constants (timeouts, sample counts, intervals) + `CORNER_MUX_CHANNEL` + MQTT topic strings — **check here first** for any timing/threshold question |
| `pins.h` | GPIO map — also documents which pins are reserved (modem, I2C bus) |
| `build_env.h` | **Generated**, gitignored — secrets from `.env` (device ID, MQTT/GSM creds) |
| `i2c_mux` | PCA9548A I2C mux (0x70) — selects which corner's NAU7802 the shared bus talks to |
| `weight_sensor` | 4× NAU7802 raw ADC reads (I2C 0x2A, one per corner behind the mux), power-down |
| `calibration` | Per-corner tare offsets + one shared span factor (NVS), raw→kg conversion, median filter |
| `temp_sensor` | DS18B20 OneWire (GPIO 25), returns NAN if sensor missing |
| `battery_sensor` | Battery voltage (ADC GPIO 35) → voltage + percent |
| `rtc_clock` | DS3231 precision RTC (I2C 0x68, upstream of the mux) — drives report scheduling via its alarm/`ext1` wake (`PIN_RTC_ALARM`, GPIO 14); uses Adafruit RTClib |
| `connectivity_mode` | GSM vs WiFi STA mode + WiFi credentials, persisted in NVS |
| `device_settings` | Tx interval (NVS) |
| `gsm_settings` | APN/user/pass + last-known cell tower IDs (NVS) |
| `mqtt_settings` | Broker host/port/TLS override (NVS) |
| `modem_manager` | SIM800L lifecycle via TinyGSM: EN-pin rail power, register, GPRS, TCP, CA cert upload; exposes `beekprModemPumpForTls()` for the SSLClient patch |
| `mqtt_client` | TLS (GSM) / plain (WiFi) MQTT publish, via PubSubClient; **TLS is done on the ESP32 (mbedTLS), not the SIM800 modem** — modem SSL is too old for modern Mosquitto |
| `wifi_manager` | WiFi STA connect/status |
| `radio_manager` | WiFi radio on/off (power saving); distinguishes mid-session power-down from pre-deep-sleep full stop |
| `telemetry_payload` | Builds the JSON MQTT payload (single function, `buildTelemetryJson`) |
| `setup_button` | GPIO 13 long-press detection (portal trigger) + short-press "abort" signal + deep-sleep ext0 wake config |
| `maintenance_portal` | Soft-AP or STA-mode web config UI: calibration, WiFi/GSM settings, mode, OTA upload |

Every module follows the same shape: `include/<name>.h` is the public API (free functions, no classes,
`<name>Begin()` init pattern), `src/<name>.cpp` is the implementation. Persisted settings modules
(`connectivity_mode`, `device_settings`, `gsm_settings`, `mqtt_settings`, `calibration`) each wrap their own NVS
namespace and expose a `*Show()` that dumps current values to serial — used by the `show` command.

## Serial command console (bench mode)

Defined in `handleCommand()` in `src/main.cpp`. Useful when changing sensor/connectivity/MQTT code — this is the
primary way to exercise it without a full field cycle: `tare`, `cal <kg>`, `show`, `reset`, `setint <min>`,
`setcell <mcc> <mnc> <lac> <cid>`, `setmode gsm|wifi`, `setwificred <ssid> <pass>`, `wificonn`, `modem`, `gprs`,
`mqttls`, `mqtt`, `send`, `sleep`, `modemoff`, `battery`, `i2cscan`, `portal`, `reboot`. Full behavior/expected-
output reference: [doc/local-setup.md](doc/local-setup.md).

`tare` zeroes all 4 corners in one pass (each corner keeps its own offset); `cal <kg>` derives one shared span
factor from the summed delta across all 4 (tared) corners — see spec.md §10 calibration procedure. `i2cscan`
scans the shared bus (expect 0x70 PCA9548A + 0x68 DS3231), then probes each mux channel in
`CORNER_MUX_CHANNEL` for its corner's NAU7802 (0x2A). `battery` prints the current battery reading plus the
`BATTERY_DIVIDER_RATIO` in effect, for per-board divider calibration (see Power chain bring-up notes above).

## Working on this codebase

- Firmware only — no test framework. Verify changes on real hardware via serial monitor + bench commands above.
- Any new timing constant, sample count, or interval belongs in `config.h`, not hardcoded at the call site.
- Any new GPIO usage: add to `pins.h` and check it doesn't collide with the reserved modem pins (4 EN, 5 RST, 16
  RX, 17 TX) or the shared I2C bus (21 SDA, 22 SCL — PCA9548A, DS3231, and every NAU7802 behind the mux all live
  here). Bare ESP32-WROOM-32 devkit — no onboard modem/PMIC, so the only other pins to avoid are the universal
  ESP32 ones: strapping pins 0/2/12/15, UART0 1/3, internal flash 6-11. See spec.md §10 for the full GPIO map.
- Persisted config (anything the maintenance portal or a `set*` serial command writes) goes in NVS via a
  `*_settings`-style module, not `config.h` defines — those are compile-time fallbacks only.
- Never commit `.env`, `certs/ca.pem`, or `include/build_env.h`/`include/ca_pem_embed.h` — all gitignored, and
  the latter two are generated at build time.
