# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for **Smart Hive Scale** — a battery-powered ESP32 (TTGO T-Call V1.3, ESP32-WROVER-B + SIM800L) beehive
weight monitor. Reads a load cell via NAU7802, wakes on an RTC schedule, and publishes JSON telemetry to Home
Assistant over MQTT — either TLS/8883 via 2G GPRS (field hives) or plain/1883 over WiFi (hive at home). Deep-sleeps
between reports to conserve battery. PlatformIO + Arduino framework, no OS.

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

There is a single build environment: `ttgo-t-call` (see `platformio.ini`). No unit test suite exists — this is
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
- **`Timer`** (ESP32's own internal RTC timer wake — distinct from the DS3231's `ext1` alarm): normally just an
  IP5306 keepalive pulse — handled by `appSchedulerContinueKeepaliveSleep()` before any other init and never
  reaches the dispatch below. The one exception is the DS3231-absent fallback: if `rtc_clock` can't find the
  DS3231 on I2C, `Timer` becomes the (imprecise, unchunked) report signal instead, since there's no `ext1`
  source to distinguish it from a keepalive pulse — see the RTC scheduling section below.
- **`PowerOn`** in GSM mode (cold boot/reflash/battery connect): 5s escape window on serial/button, then same
  headless publish-and-sleep as `RtcAlarm` (a blocking multi-minute GSM cycle otherwise locks out the console).
- **`Button`** (or WiFi-mode power-on): interactive **bench mode** — serial command console + live weight
  readings for 5 minutes (`BENCH_STAY_AWAKE_MS`), extended by any serial input. On expiry, publishes once and
  sleeps. In WiFi mode the maintenance portal also comes up automatically once connected.

Publish cycle (`appSchedulerRunPublishCycle`, in `app_scheduler.cpp`): 2-minute thermal warm-up (skipped if
already awake that long) → read NAU7802 + DS18B20 + battery → connect (GSM: SIM800L register → GPRS attach → TLS
MQTT :8883; WiFi: STA connect → plain MQTT :1883) → publish `state` + `availability` → disconnect. Retries with
backoff up to `PUBLISH_MAX_ATTEMPTS`; setup button or serial input aborts into bench mode at any wait point.
Sensor readings — weight, temp, battery, and the `report_time` timestamp — are captured as a `SensorSnapshot`
(`mqtt_client.cpp::captureSensorSnapshot()`) *before* any modem/WiFi activity, deliberately — GSM registration,
GPRS attach, and the TLS handshake all draw current spikes (SIM800 TX bursts up to ~2A) on the same shared
supply the NAU7802's bridge excitation and the DS3231's I2C bus run from, and sampling (or reading the RTC)
mid-spike can corrupt the result (fixed 2026-07-17; it previously sampled weight *and* read the RTC *after*
connecting — the RTC half of this was only caught after a field report showed a garbled `report_time` with an
out-of-range month/day/minute, decoded from a glitched I2C read). `rtcClockNowIso8601()` also validates every
field is in-calendar-range before formatting, so a future glitch degrades to `report_time: null` instead of a
malformed string. Network-status fields (rssi, cell tower, WiFi link) are read separately, after connecting,
since those only exist once actually on the network.

Before every deep sleep: NAU7802 → register power-down, WiFi fully stopped, modem powered off,
`ip5306EnsureBoostKeepOn()` re-armed, DS3231 report alarm reprogrammed (below).

### RTC-driven scheduling (`rtc_clock`) and the IP5306 keepalive quirk (battery-critical)

A DS3231 (I2C 0x68, shares the IP5306's bus on GPIO 21/22) drives the report schedule via its
alarm output (`PIN_RTC_ALARM`, `ext1` deep-sleep wake) so reports land on precise wall-clock time
instead of drifting on the ESP32's own RC-oscillator timer. `appSchedulerEnterDeepSleep()`
reprograms the alarm (`rtcClockSetNextAlignedAlarm()`, Alarm2/minute precision) on every real sleep
entry to fire at the *next wall-clock boundary* `settingsTxIntervalSec()` apart — e.g. a 1h interval
always fires at :00, not "1h from whenever this cycle happened to finish" (fixed 2026-07-17; the
original version anchored to "now", so the exact minute it fired at slowly wandered based on how
long each cycle's connect/publish took). Alignment is to midnight, so intervals that evenly divide
1440 minutes (30, 60, 360, ...) land on round clock times; others still fire on a consistent
schedule, just not necessarily round numbers. If the DS3231 isn't found at boot, `rtc_clock` logs
an error and `app_scheduler` falls back to sleeping the full interval on the ESP32's internal timer
in one shot from "now" (unaligned, unchunked) — see the `Timer` `WakeCause` note above.

A never-set DS3231 boots reporting `lost_power=true` and its power-on-reset epoch — cosmetically
wrong but harmless to scheduling (Alarm2 matches hour:minute only, not the date). Before
reprogramming the alarm, `appSchedulerEnterDeepSleep()` calls `rtcClockSyncFromSystemTimeIfNeeded()`,
which writes the ESP32's system clock into the DS3231 once that clock itself is plausible —
currently only `modemManagerSyncClock()` (GSM mode, NITZ/NTP over GPRS, called during
`modemManagerEnsureGprs()`) ever sets it via `settimeofday()`. WiFi mode has no clock source yet,
so a DS3231 paired with a WiFi-only device won't self-correct.

Separately, some IP5306 clones auto-cut the 5V boost rail after ~32s under near-zero load (i.e.
exactly what ESP32 deep sleep looks like), and on top of that some units are entirely unreachable
over I2C — both were confirmed on a specific V1.3 unit (2026-07-14/15 debugging session), which
was replaced with a working V1.3→V1.4 board rather than chased further in software. USB power
masks the symptom (VBUS backs the rail directly), so this only shows up running on battery — if
the rail cuts, the whole board dies and stays dead until physically power-cycled, RTC memory and
all.

Two layers of defense, both keyed off one honest signal — `ip5306BoostKeepOnOk()`:
- `ip5306SetBoostKeepOn()` (`ip5306.cpp`) writes the "keep boost on" register bit and **verifies it via
  read-back** rather than trusting a successful I2C ack — a bad PMIC can ack the write without the bit sticking.
  This was a real bug (fixed 2026-07-14): it previously reported success unconditionally.
- `app_scheduler.cpp::appSchedulerEnterDeepSleep()` only arms the ESP32's internal timer for
  `IP5306_KEEPALIVE_CHUNK_SEC` (25s, in `config.h`) — waking to draw real current and reset the PMIC's own
  shutoff timer — when `ip5306BoostKeepOnOk()` comes back false. A verified-good PMIC sleeps on the DS3231
  alarm alone, no internal timer armed at all. This keeps the keepalive tax off hardware that doesn't need it,
  while still self-healing on a future flaky board with no firmware change (and `boost_keep_on` in the MQTT
  payload surfaces the regression immediately if it ever happens). Since scheduling now lives entirely on the
  DS3231's independent `ext1` alarm, a keepalive pulse (`Timer` wake) never needs to know how much of the report
  interval remains — it just re-arms the same fixed 25s window and goes back to sleep.
  `main.cpp::setup()` checks for this case (`rtcClockBegin()` + `appSchedulerContinueKeepaliveSleep()`) *before*
  any NVS/sensor/radio init, so each pulse costs a handful of ms rather than a full sensor re-init — don't move
  expensive init above that check.

## Module map (`src/` + `include/`, one .cpp/.h pair each)

| Module | Responsibility |
|---|---|
| `app_scheduler` | Orchestrates the wake cycle above; owns deep sleep / retry logic |
| `main.cpp` | `setup()`/`loop()`, serial command dispatch, bench-mode state (no header) |
| `config.h` | All tunable constants (timeouts, sample counts, intervals) + MQTT topic strings — **check here first** for any timing/threshold question |
| `pins.h` | GPIO map — also documents which pins are reserved (modem, PMIC) |
| `build_env.h` | **Generated**, gitignored — secrets from `.env` (device ID, MQTT/GSM creds) |
| `weight_sensor` | NAU7802 raw ADC reads (I2C, dedicated bus GPIO 18/19), power-down |
| `calibration` | Tare/scale coefficients (NVS), raw→kg conversion, median filter |
| `temp_sensor` | DS18B20 OneWire (GPIO 25), returns NAN if sensor missing |
| `battery_sensor` | Battery voltage (ADC GPIO 35) → voltage + percent |
| `ip5306` | PMIC I2C (GPIO 21/22, address 0x75) boost-keep-on control |
| `rtc_clock` | DS3231 precision RTC (shares GPIO 21/22, address 0x68) — drives report scheduling via its alarm/`ext1` wake (`PIN_RTC_ALARM`, GPIO 14); uses Adafruit RTClib |
| `connectivity_mode` | GSM vs WiFi STA mode + WiFi credentials, persisted in NVS |
| `device_settings` | Tx interval (NVS) |
| `gsm_settings` | APN/user/pass + last-known cell tower IDs (NVS) |
| `mqtt_settings` | Broker host/port/TLS override (NVS) |
| `modem_manager` | SIM800L lifecycle via TinyGSM: power, register, GPRS, TCP, CA cert upload; exposes `beekprModemPumpForTls()` for the SSLClient patch |
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
`mqttls`, `mqtt`, `send`, `sleep`, `modemoff`, `portal`, `reboot`. Full behavior/expected-output reference:
[doc/local-setup.md](doc/local-setup.md).

## Working on this codebase

- Firmware only — no test framework. Verify changes on real hardware via serial monitor + bench commands above.
- Any new timing constant, sample count, or interval belongs in `config.h`, not hardcoded at the call site.
- Any new GPIO usage: add to `pins.h` and check it doesn't collide with the reserved modem (4,5,23,26,27) or PMIC
  (21,22) pins. **On V1.4 boards, GPIO 32/33 are also modem DTR/RI (not wired at all on V1.3 — this bit us once:
  `PIN_RTC_ALARM` was originally picked as GPIO 32 before that was known) and GPIO 13 has an onboard LED that may
  or may not conflict with `PIN_SETUP_BUTTON`, which already uses it** — don't assume a pin is free from the
  V1.3 reservations alone.
- Persisted config (anything the maintenance portal or a `set*` serial command writes) goes in NVS via a
  `*_settings`-style module, not `config.h` defines — those are compile-time fallbacks only.
- Never commit `.env`, `certs/ca.pem`, or `include/build_env.h`/`include/ca_pem_embed.h` — all gitignored, and
  the latter two are generated at build time.
