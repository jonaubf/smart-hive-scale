# SIM800L (standalone) — wiring, power control, and bring-up

The discrete build uses a **bare SIM800L module** — no integrated PMIC or onboard step-down regulator, unlike
the retired TTGO T-Call design. It needs its own supply rail, UART level-shifting, and power control. Full BOM
and rationale: [spec.md §5/§10](../spec.md#10-hardware-connections).

## Wiring

| SIM800L | Connects to | Notes |
|---------|--------------|-------|
| VCC | CR-SJ5530 #2's **4.2 V** tap | Plus a **1000 µF bulk cap** (and optionally a 5.1 V Zener) right at this pin — see "Power chain bring-up" below |
| GND | Common GND | |
| TXD | 1 kΩ series → ESP32 GPIO 16 | Module output (2.8 V) reads fine on ESP32's 3.3 V input, no divider needed |
| RXD | 1 kΩ series + 5.6 kΩ shunt-to-GND → ESP32 GPIO 17 | **Required** — ESP32's 3.3 V would otherwise exceed the module's UART input range |
| RST | ESP32 GPIO 5 | Active low, emergency reset only |

![ESP32 GPIO17 (TX2) → SIM800L RXD divider: 1kΩ series + 5.6kΩ shunt-to-GND, 3.3V → ~2.8V](sim800l_uart_divider.svg)

**VBAT is 3.4–4.4 V, 4.0 V recommended — not 5 V.** Per the module's datasheet
(`doc/Datasheet_SIM800L.pdf`, Table 41 Absolute Maximum Ratings), VBAT above 4.5 V "will cause permanent
damage." CR-SJ5530 #2 must be set to its **4.2 V** tap — the 5 V tap would kill the module, the 3.3 V tap risks
under-voltage shutdown during TX current bursts (which sag the rail by up to ~350 mV).

**UART is 2.8 V logic, not 3.3 V-tolerant on its inputs.** Only `RXD` (ESP32 → SIM800L) needs the divider —
this design doesn't wire RTS/CTS/DTR. `TXD` (SIM800L → ESP32) only needs a series 1 kΩ for protection.

## Power control — no `PWRKEY` pin

This module's `PWRKEY` is tied to GND on the PCB — there's no exposed pin to pulse, and no software path to
soft-power-down and re-trigger it. **The module auto-boots the instant VCC is applied.** Power control therefore
happens entirely at the rail:

- **Power on:** ESP32 GPIO4 drives CR-SJ5530 #2's `EN` pin high → the converter's 4.2 V output comes up → the
  SIM800L auto-boots. Firmware waits for it to answer `AT` (also syncs autobaud).
- **Power off:** send `AT+CPOWD=1` first (clean network detach, a UART command — needs no `PWRKEY`), then drive
  `EN` low a moment later to actually cut power. Never trust the post-`CPOWD` state alone; only `EN` low
  guarantees the rail is off.

One consequence for firmware: `EN` is enabled by default and GPIO4 floats until firmware configures it, so **on
every cold boot the modem rail comes up (and the SIM800L auto-boots) before `setup()` runs** unless GPIO4 is
claimed and driven low immediately — see `main.cpp::setup()`'s first statements. The same applies across **deep
sleep**: the ESP32 releases GPIO output state on sleep entry, so an unheld `LOW` floats, `EN` re-enables, and
the modem runs (draining the battery) through the whole sleep window. GPIO4 is an RTC-domain pin — it's latched
with `gpio_hold_en()` before every sleep and released after wake, before driving it again. See CLAUDE.md's
"Modem rail control and the GPIO-hold trap" section — this is the single largest battery-drain risk in the
whole design.

## Power chain bring-up

**A battery must be connected to the TP4056 — the module cannot run on USB/TP4056 alone.** The TP4056 is a
charge controller, not a supply: its current limit (~1 A on typical modules) can't source the SIM800L's ~2 A
boot-current bursts. Without a battery to buffer them, the whole 3.3 V/4.2 V chain oscillates (observed as
~3.1–4.2 V swings on a multimeter) and resets everything downstream repeatedly — this looks exactly like a dead
modem even though the module itself may be fine. Always bench-test with a battery wired to TP4056 `B+`/`B−`.

**Avoid Quick Charge (QC2.0/3.0) USB bricks** as the TP4056's input — they negotiate output voltage via a D+/D−
handshake the TP4056 doesn't speak, and an unrecognized brick can default to boosting past 5 V, which is out of
spec for the charger IC. Use a plain fixed-5V adapter.

**The 1000 µF bulk capacitor must be soldered right at the SIM800L's VCC/GND pins**, not on a breadboard rail or
a few cm away — wire inductance between the cap and the module defeats it during the boot-current burst. Thick,
short VCC/GND wiring (or PCB traces) matters more here than almost anywhere else in the design.

### Diagnosing a modem that won't come up

Run `modem` (see [`local-setup.md`](local-setup.md#modem-test)) and watch the module's own netlight LED:

| Symptom | Likely cause |
|---------|--------------|
| LED never lights, VCC reads a steady ~4.2 V on a multimeter | Averaging meter hiding millisecond-scale brownout dips during boot — almost always **no battery connected** (TP4056-only power); confirm by wiring a battery |
| LED never lights, rhythmic ticking/buzzing noise from the converter | Boot-retry loop — same brownout cause as above; check bulk cap placement and wire gauge next |
| LED blinks steadily (~1/s) but firmware still reports `ERR modem not answering AT after rail-up` | Module booted fine — this is a **UART wiring** problem. Check GPIO17→RXD goes through the divider (not straight through), and GPIO16←TXD has its series resistor. A miswired divider leaves the module unable to read valid logic levels |
| LED dark, and RST (GPIO 5) doesn't read high | RST held low — check that pin, though this is uncommon since firmware drives it high before rail-up |

If a battery is connected, the bulk cap is placed correctly, and the LED still never blinks: isolate the
converter by temporarily wiring the SIM800L's VCC straight to the battery node (same GND, everything else
unchanged). If the LED starts blinking on direct battery power, CR-SJ5530 #2 (or its wiring) is the bottleneck;
if it stays dark even then, suspect the module itself (prior overvoltage exposure, or DOA).

## Official resources

- [SIM800 series AT command manual and datasheet](../doc/Datasheet_SIM800L.pdf)
- Full pin tables and the rest of the wiring diagram: [`local-setup.md` — Wiring](local-setup.md#wiring)
- Project spec: [`spec.md` §10](../spec.md#10-hardware-connections)
