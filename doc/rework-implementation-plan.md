# Discrete rework — firmware implementation & bring-up record

**Status: Phases A–D (firmware) and E1–E4 (bench bring-up) complete.** This is now a historical
record of the migration from the retired TTGO T-Call firmware (preserved at the `tcall-v1` git
tag) to the current discrete ESP32-WROOM-32 + 4-corner build — kept for the hard-won bring-up
facts (EN threshold measurements, power-chain gotchas, calibration values) that aren't visible
from the code alone. For day-to-day work on the shipped firmware, see [CLAUDE.md](../CLAUDE.md)
and [spec.md](../spec.md) instead; this file isn't the place to look for current architecture.

**Phase ↔ spec §12 mapping:** A+B → step 12 (power chain), C → steps 13–14 (mux + calibration),
D → step 15 (SIM800L on new wiring, integration), E → steps 12/15 bench portions + step 16
(deployment).

---

## Phase A — repo & build scaffold

**A1. Preserve the T-Call firmware.** `git tag tcall-v1` on the last T-Call-buildable commit and
push the tag. The deployed T-Call board stays reflashable from that tag forever; `main` moves on.
Don't maintain two build environments/`#ifdef` hardware profiles — the T-Call is retired, not
co-supported.

**A2. `platformio.ini`.** Rename env `ttgo-t-call` → `esp32-discrete`, `board = esp32dev`, drop
`-DBOARD_HAS_PSRAM` (plain WROOM, no PSRAM). Everything else stays: all four `extra_scripts`
(`load_env.py`, `embed_ca.py`, `patch_tinygsm.py`, `patch_sslclient.py` — TLS-over-2G is
unchanged, the patches are still required) and all `lib_deps` (no library for the PCA9548A —
plain `Wire`). Regenerate `compiledb` after.

**A3. `pins.h`.** Replace the T-Call map with spec §10's:

| Signal | GPIO |
|---|---|
| Modem UART2 TX / RX | 17 / 16 (T-Call used 26/27) |
| Modem rail enable (`EN`) | 4 |
| Modem RST | 5 |
| I2C SDA / SCL (single shared bus) | 21 / 22 |
| DS18B20 OneWire | 25 |
| DS3231 SQW/INT (ext1 wake) | 14 |
| Setup button (ext0 wake) | 13 |
| Battery ADC | 35 |

Drop every T-Call reservation comment (IP5306 bus, modem pins 4/5/23/26/27, V1.4 DTR/RI); the
new "avoid" list is just the universal ESP32 ones (strapping 0/2/12/15, UART0 1/3, flash 6–11).

**A4. `config.h`.** Add: `I2C_MUX_ADDR 0x70`, `NUM_CORNERS 4`, modem-boot timeout (wait for
`RDY`/`AT` after `EN` high), `EN`-settle delay, post-`CPOWD` delay before `EN` low. Remove all
`IP5306_*` constants. Per-corner sample counts reuse the existing sample/median constants.

*Verify:* Phase A alone won't compile (code still references `ip5306`) — land A+B as one unit.

---

## Phase B — power model rewrite (remove IP5306, add `EN` rail control)

**B1. Delete the `ip5306` module** and everything keyed off it: the keepalive-chunk logic in
`app_scheduler` (`appSchedulerContinueKeepaliveSleep()`, the 25 s `Timer`-wake path), the
early-boot fast-path check in `main.cpp::setup()`, and the `boost_keep_on` payload field in
`telemetry_payload`. `WakeCause::Timer` keeps exactly one meaning afterward: the DS3231-absent
fallback report signal. The wake-cause dispatch in `main.cpp` gets simpler — document that in
the code where the old comment explained the keepalive quirk.

**B2. Modem rail off, first thing in `setup()`.** `EN` is enabled by default and GPIO4 floats
until configured, so the SIM800L auto-boots on every cold boot before firmware runs (spec §10).
The very first statements in `setup()` — before NVS, sensors, anything — must claim GPIO4 as
output-low. **And critically: hold it through deep sleep.** A plain `digitalWrite` releases when
deep sleep starts, the pin floats, `EN` re-enables, and the modem burns battery all night. GPIO4
is an RTC-domain pin, so use `gpio_hold_en(GPIO_NUM_4)` + `gpio_deep_sleep_hold_en()` before
every sleep entry (and `gpio_hold_dis` after wake, before driving it again). This is the #1
battery-life landmine in the new design — treat it like the T-Call's boost-keep-on bit: verify,
don't assume.

**B3. `modem_manager` power control.** Replace the T-Call's PWRKEY/board-power sequence:
- *Power on:* `gpio_hold_dis`, drive GPIO4 high, wait for the module's auto-boot (`RDY` URC or
  `AT` responding, with the timeout from A4).
- *Power off:* `AT+CPOWD=1` first (clean network detach — the datasheet path, needs no PWRKEY),
  short delay, then GPIO4 low + re-hold. Never trust post-`CPOWD` state without cutting the rail.
- `RST` on GPIO5 unchanged (emergency reset only).
- Serial `modem` / `modemoff` commands map onto these; UART2 init moves to pins 17/16.

*Verify:* builds; on a bare devkit, `modem`/`modemoff` toggle GPIO4 measurably (multimeter), and
GPIO4 stays low through a `sleep` → button-wake cycle.

---

## Phase C — sensing rework (mux + 4 corners)

**C1. New `i2c_mux` module** (same shape as every other module: `include/i2c_mux.h` free
functions + `src/i2c_mux.cpp`): `i2cMuxBegin()` (probe 0x70, log error if absent),
`i2cMuxSelect(uint8_t channel)` (write `1 << channel`), `i2cMuxDeselectAll()` (write `0x00` —
do this after each corner read so a hung NAU7802 can't wedge the shared bus that the DS3231
also lives on).

**C2. One I2C bus.** `weight_sensor` drops its private bus on GPIO 18/19 and joins the shared
`Wire` on 21/22. Single `Wire.begin()` in `main.cpp`; init order: `Wire` → `i2cMuxBegin()` →
`rtcClockBegin()` → `weightSensorBegin()`.

**C3. `weight_sensor` goes multi-channel.** Begin/read/power-down iterate corners 0–3 through
the mux. API returns a per-corner result (raw + ready flag) so one dead corner degrades to a
flagged outlier (FR-11), not a failed cycle. Register power-down before sleep now loops over
all 4.

**C4. `calibration` schema v2.** NVS: 4 per-corner tare offsets + 1 shared span factor (spec
§10 calibration procedure). New namespace/version — no migration needed, this hardware starts
fresh. Conversion returns per-corner kg (tared, shared-scale) and the sum; median filter per
corner, then sum of medians. `calibrationShow()` prints all 5 values.

**C5. Serial console updates** (`handleCommand()` in `main.cpp`):
- `tare` — all 4 corners in one pass (each gets its own offset), same stability checks per corner.
- `cal <kg>` — shared span from the summed delta.
- `show` / live reading line — per-corner: `c0=… c1=… c2=… c3=… weight_kg=… stable_kg=…`.
- `i2cscan` — repurpose from "PMIC bus scan" to: scan the main bus (expect 0x70, 0x68), then
  each mux channel (expect 0x2A) — this becomes the step-13 bring-up tool.

**C6. `telemetry_payload`.** Add `corner1_kg`..`corner4_kg` (corner index order — mux channels
0, 1, 2, 7 per `CORNER_MUX_CHANNEL`, spec §7); `boost_keep_on` is already gone from B1.

**C7. `maintenance_portal` calibration page.** Live per-corner readings, one tare-all button,
span-calibrate with known kg (portal UI per spec §9). OTA/settings sections unchanged — FR-9b
(OTA over soft-AP *and* STA LAN) is already today's portal behavior and carries over.

*Verify:* builds; bench first with 1 NAU7802 behind one mux channel, then all 4. `i2cscan`
output matches expectations. Tare/cal/show round-trip through NVS.

---

## Phase D — scheduler integration & gap-closing

**D1. `app_scheduler` publish cycle.** Preserve the snapshot-before-connect order exactly
(CLAUDE.md's 2026-07-17 lesson, restated in spec §6 wake cycle): warm-up → capture all 4
corners + DS18B20 + battery + DS3231 timestamp → *then* radio. The snapshot struct grows the 4
corner fields. Warm-up powers all 4 channels.

**D2. `rtc_clock`.** Logic unchanged (aligned Alarm2 scheduling, validated ISO8601, DS3231-absent
fallback to internal timer). Only the bus context changes — and the fallback text/logs should
stop mentioning IP5306 keepalive.

**D3. WiFi clock source (recommended, small).** Add SNTP on WiFi connect → `settimeofday()` →
the existing `rtcClockSyncFromSystemTimeIfNeeded()` path. Closes the long-standing "WiFi mode
has no clock source, DS3231 never self-corrects" gap noted in CLAUDE.md and local-setup.md.
Keep the GSM rule: sync on every connect, never trust a merely-"plausible" clock (the ~35 s/cycle
drift lesson).

**D4. Home Assistant.** In `doc/home-assistant/mqtt_sensors.yaml`: uncomment the 4 prepared
corner-weight entities; delete the `Boost keep-on` binary sensor (field no longer published).

*Verify:* full `send` cycle in WiFi mode on the bench devkit — 4 corners in the payload,
`report_time` sane after an SNTP sync, HA shows corner entities.

---

## Phase E — hardware bring-up & deployment (needs the real parts)

**E1. Power chain alone first** (nothing downstream connected):
1. Confirm output-tap selection on both CR-SJ5530s (#1 → 3.3 V, #2 → 4.2 V) with a multimeter
   before wiring loads — spec §10 warns the 5 V tap is a one-mistake modem killer.
2. Do the `PS`-pad short + indicator-LED removal on **both**; measure standby current (<100 µA
   each, spec BOM).
3. **`EN` threshold bench test** — **done, verified 2026-07-29**: measured internal
   `VIN —620 kΩ— EN —1.1 MΩ— GND` divider ⇒ float ≈0.64×VIN ⇒ threshold <1.9 V, so 3.3 V
   high works at any VIN. ESP32-driven test passed: off at boot, 4.2 V on `modem`, ~0 V on
   `modemoff`, and stays off through deep sleep (GPIO hold confirmed). Wire GPIO4 → `EN`
   direct or ≤1 kΩ series (spec §10).
4. Ideal-diode module orientation (`IN` ← CR-SJ5530 #1, `OUT` → ESP32 `3V3`) verified against
   the board silkscreen; ESP32 boots on battery; both rails hold under load.

**E2. Battery divider calibration — done (2026-08-08).** Single-point calibrated on the first
bring-up unit via the `battery` bench command against a multimeter on the raw battery node:
old nominal ratio 2.1314 reported 4.026 V vs. 4.112 V actual ⇒ `BATTERY_DIVIDER_RATIO = 2.1769`
(config.h). Recalibrate the same way per board — real divider resistors don't land on exactly
2.000, matching the T-Call precedent (~6.6% off nominal there too).

**E3. I2C bring-up — done (2026-08-08)** (spec step 13): `i2cscan` confirmed 0x70 + 0x68
upstream and 0x2A on all 4 of channels 0, 1, 2, 7 (`CORNER_MUX_CHANNEL`) on the first bring-up
unit. One corner initially showed a continuously-ramping/wrapping raw reading (floating bridge
input) — traced to a load-cell wiring fault on that corner, not a firmware or mux issue; resolved
by fixing the E+/E−/A+/A− connection.

**E4. SIM800L bring-up — done (2026-08-08 modem, 2026-08-15 full chain).**
`modem` succeeded on the discrete wiring (`EN` rail-up → `AT` sync → SIM ready → registered on
Kyivstar, rssi=12, real cell tower IDs), confirming EN-based power control, the UART divider, and
the bulk cap. Full GSM→TLS→MQTT chain since verified in the field from the broker side: repeated
scheduled cycles show TCP+TLS+auth in ~2s (`TLSv1.2 ECDHE-RSA-AES256-GCM-SHA384`, `hive-01`
authenticated), telemetry landing in HA ~1s later, and a clean close — full session ~6s.
Two MQTT-layer defects were found and fixed only once running on a real 2G link, both invisible
on the bench: PubSubClient's 15s default keepalive (too thin for a slow uplink) and the MQTT
DISCONNECT being destroyed by TinyGSM's quick close — see CLAUDE.md "Closing an MQTT session over
GSM" for the mechanism, and do not undo those two.
**Real-world gotcha found during bring-up: the TP4056 cannot power the board on its own.**
Running on USB/TP4056 with no battery attached caused the whole supply to oscillate ~3.1–4.2 V
and reset the board the moment the modem attempted its boot-current burst (LED never lit, looked
exactly like a dead/miswired modem). Confirmed root cause: TP4056's current limit can't source
the ~2 A burst; a battery buffers it. **A battery must be connected for any modem bring-up or
sleep-current testing, USB-only is not sufficient.** Also found: Quick Charge (QC2.0/3.0) USB
bricks are unsafe on the TP4056 input — an unrecognized-negotiation QC brick can default-boost
past 5 V, which is out of spec for the charger IC; use a plain fixed-5V adapter. Both lessons
folded into CLAUDE.md and `doc/esp32_sim800l.md`.

**E5. Sleep-current audit — root cause found, final number pending re-verification.** Initial
battery-only measurement (USB fully disconnected) read ~3 mA in series with the battery — an
order of magnitude over the <300 µA budget. Root cause: **the sensor VCC rail is always-on
through deep sleep by design** (CR-SJ5530 #1 feeds ESP32 `3V3` + all sensor VCCs directly, same
rail the ESP32's own RTC memory needs), so `weightSensorPowerDown()`'s register-level power-down
quiesces each NAU7802's own ADC current but cannot touch anything hardwired straight to that
rail — specifically, the stock power-indicator LEDs on the ESP32 devkit and on all 4 NAU7802
breakout boards, none of which are under firmware control. These must be desoldered (LED or its
series resistor) per board — this is a hardware step, not a firmware fix, and it isn't optional
for any unit built from off-the-shelf breakout boards. **Re-run the sleep-current measurement
after LED removal on any new unit** before trusting it's under budget; a clean `PS`-pad short
and GPIO4 hold (already verified in E1) get you most of the way, but a single un-removed LED
alone can cost several mA — more than the entire rest of the budget combined.

**E6. Real calibration + burn-in** (spec step 16 prep): cells mounted on the frame, tare-all +
span with a known weight, verify per-corner sanity. Battery burn-in *without USB attached* —
watch `mosquitto_sub` for on-schedule reports (the T-Call lesson: the serial monitor resets the
board and fakes wakes; MQTT is the only honest observer). **Not yet performed** on a
field-mounted unit as of this record — see `doc/user-guide.md` §3/§7 for the procedure.

**E7. Field deployment + docs — docs done (2026-08-08).** Operator docs rewritten for the shipped
discrete-build reality: `doc/local-setup.md`, `doc/user-guide.md`, `doc/esp32_sim800l.md`
(repurposed for the standalone module), `CLAUDE.md`, `README.md`. All "rework in progress"
framing and TTGO/T-Call-specific instructions removed from the living doc set; the retired
firmware remains available at the `tcall-v1` git tag for reference. **Physical field
installation itself is still pending** — do E6 (real calibration + burn-in) first.

---

## Carry-over invariants (do not regress)

- **Sensor snapshot strictly before any radio activity** — weight, temp, battery, timestamp
  (spec §6 step 3; hard-won on the T-Call).
- **Honest verification over assumed success** — read back what you wrote (the boost-keep-on
  lesson applies to GPIO4 hold state and mux channel selection alike).
- New timing constants → `config.h`; new GPIO → `pins.h`; portal/serial-writable settings →
  NVS `*_settings` modules; never commit `.env` / certs / generated headers.
- Wall-clock-aligned DS3231 scheduling, re-sync clock on every GSM connect, validate RTC fields
  before formatting (`report_time: null` over garbage).
