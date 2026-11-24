# DC Electronic Load: Rev A, built and bench-tested

![Rev A on the bench: control board stacked on the power board, on a heat-pipe tower heatsink](bench_test/photos/board_on_bench.jpg)

A programmable DC electronic load for testing power supplies and buck converters. **Rev A** is the board that was
built (Nov 2024) and characterised on the bench (Nov 2026): an STM32F103 control board stacked on a power board with
two IRF530 MOSFETs, each in its own op-amp current loop, cooled by a heat-pipe tower heatsink. It is controlled over
USB-C, with a Windows/macOS GUI.

| What | Where |
|---|---|
| Hardware (Altium schematics, PCB, gerbers, BOM, netlists) | [`hardware/`](hardware/README.md) |
| Firmware (STM32, safety ceilings from these measurements) | [`firmware/`](firmware/README.md) |
| Control GUI (Windows, macOS) | [`gui/`](gui/README.md) |
| Bench report, raw data, instrument drivers | [`bench_test/`](bench_test/README.md) |
| Plan for the next board | [`RevB/`](RevB/README.md) |

## The circuit

![Power stage: two LMV358 loops driving IRF530 Q1/Q2, 0.2 Ω pairs R12-R15 to the shared Rshunt node](hardware/images/power_stage_mosfets.png)

| VSEN difference amp (U1B) and current sense (INA240 on R5) | DAC (MCP4725) and 4.096 V reference (REF5040) |
|---|---|
| ![](hardware/images/power_monitor_vsen_isen.png) | ![](hardware/images/dac_and_reference.png) |

Full schematics: [power board](hardware/power_board/power_board_schematic.pdf), [control board](hardware/control_board/control_board_schematic.pdf).

Each IRF530 has its own LMV358 loop (U3A/U3B). The loop forces its source node (R12 ∥ R13 or R14 ∥ R15, then the
shared 5 mΩ shunt R5) to follow `Control_Voltage`, which is 0.15 × the MCP4725 DAC output (R16/R17). The op-amps and
the DAC both run from the 4.096 V reference, so the gate can never exceed about 4.1 V, the main limit (P1 below).
U1B's feedback resistor R1 goes to its **+** input, which latches VSEN high (P2).

![Close-up of the stacked boards](bench_test/photos/board_closeup.jpg)

## Bench setup, equipment and methods

![Bench: RIGOL DHO804, KORAD KA3005P as the DUT, Brymen BM235, ST-Link, load on the heat-pipe tower](bench_test/photos/bench_setup.jpg)

| Equipment | Use |
|---|---|
| **KORAD KA3005P** (0-30 V, 0-5 A) | the DUT: constant voltage, its current limit set before every output-on as the hardware cap; its ammeter (1 mA) is the current reference |
| **RIGOL DHO804** (70 MHz, 12-bit, 4 ch), 10× probes | gate, source and rail waveforms over USBTMC; probe gains checked against the DMM on one node |
| **Brymen BM235** DMM + K-type thermocouple | DC reference for rails and the source-path survey (mV range, 0.01 mV); heatsink temperature |
| ST-Link V2 + OpenOCD | firmware flashing |
| Phone camera (1080p video) | thermocouple record, read frame by frame |
| Windows PC, Python (pyserial, pyvisa / NI-VISA) | automation: one script per test, safety monitor, logging |

**Methods.**
- **Locked-down test firmware.** The board ran test firmware that powers up with the DAC at 0, needs an `ARM`
  command, and caps the DAC. It switches off after 2 s without a command, on over-current, and on watchdog reset.
- **Automated, logged runs.** Every test script prints its plan and sets the PSU current limit before the output
  goes on. It runs a 3 Hz safety monitor that aborts in a fixed order: DAC 0, disarm, PSU off. It writes a run
  folder with the data, the instrument IDs, the probe map and the scope captures.
- **DC values** come from the DMM and the PSU. The scope is used for waveforms, ratios and relative changes; its
  offset error at large offsets was measured and avoided. **Currents** come from the PSU ammeter, cross-checked
  against the load's own current readback (ISEN).
- **Transients** were captured at 20 MHz bandwidth, triggered on the gate voltage, single-shot or 16× averaged.
- **Thermal:** the thermocouple sat on the heatsink at Q2's thermal pad and was filmed. 90 frames were read and
  synced to the PC log using four steps visible on the PSU display (±1.5 s). A two-pole thermal model was fitted to
  the readings, and the die temperature was estimated from the gate voltage at constant current, with its
  temperature coefficient (−3.7 mV/K) calibrated from the same data.
- Ambient 20 °C, no fan, control board powered from USB only.

## Measured specification

| Parameter | Rev A, measured |
|---|---|
| Current | **0 to 1.93 A** (2.04 A when hot); 1.90 A ceiling in firmware |
| Setpoint | 2.5 mA steps, no dead zone, 5 mA at code 0; −8 % gain uncalibrated, ±7 mA calibrated (0 to 1.83 A) |
| Current readback | 311 counts/A, ±10 mA to 1.9 A |
| Voltage readback | does not work (P2) |
| Minimum DUT voltage | 0.25 V @ 0.5 A, 0.55 V @ 1 A, 1.7 V @ 1.9 A (0.56 Ω full-on) |
| Continuous power, no fan | **≈ 36 W** for Tj ≤ 125 °C (band 29 to 48 W) |
| Load steps | 11 to 16 µs (10-90 %), ≤ 1.5 % overshoot, no ringing, 0.1 to 1.8 A |
| Current sharing | +0.1 % between the two MOSFETs |
| Interface | USB-C (CH340, 115200), bus-powered |

## Operating envelope

![Operating envelope](bench_test/results/2026-11-21_202620_p3_8_thermal/plot_operating_envelope.png)

| DUT voltage | Max current | Limited by |
|---|---|---|
| 0.3 V | ≈ 0.5 A | full-on resistance 0.56 Ω |
| 0.6 V | ≈ 1.0 A | full-on resistance |
| 1.7 to 15 V | **1.93 A** | gate drive (P1) |
| 20 V | ≈ 1.8 A | thermal (≈ 36 W) |
| 30 V | ≈ 1.2 A | thermal |

The firmware and GUI are more conservative: 35 W peak, and a thermal cutoff at the equivalent of 30 W for 3 minutes
(≈ 23 W continuous).

## Test cases

Scope captures are shown labelled: the raw screenshot with a title, the test conditions, a channel legend (signal, probe point, scale, offset) and call-outs. The originals stay in each run folder under [`bench_test/results/`](bench_test/results); all 40 labelled captures are in [`bench_test/scope_annotated/`](bench_test/scope_annotated). Each sits next to the Python plot of the same run.


### 1. Current capability: where the load stops (P1)
**Method:** PSU at 3.0 V, current limit set 0.2 A above each tier. The DAC was swept in 4 tiers (0.5 / 1 / 2 / 3 A
caps), logging the PSU current, ISEN, both gate voltages and both source voltages, until a gate reached its measured
ceiling.
**Result:** the current flattens at **1.933 A** with both gates pinned at 4.07 V, the op-amps' 4.096 V supply.
The ceiling with no DUT was 4.094 V, at every DAC code.

| | |
|---|---|
| ![](bench_test/results/_combined/plot_3_1_req_vs_delivered.png) | ![](bench_test/results/_combined/plot_3_1_gate_vs_current.png) |
| ![](bench_test/results/_combined/plot_3_1_sharing.png) | ![](bench_test/results/2026-11-21_173942_p2_dac_transfer/plot_gate_ceiling.png) |

### 2. Accuracy and calibration
**Method:** codes 0 to 40 in steps of 2, then 50 to 850 in steps of 25, at 3 V, against the PSU ammeter.
**Result:** no dead zone. **I = 2.516 mA/code + 10.5 mA**, within ±6.8 mA, against 2.727 mA/code designed (−8 %).
ISEN: 311.03 counts/A, within 0.6 % of design.

| | |
|---|---|
| ![](bench_test/results/2026-11-21_183314_p3_3_linearity/plot_3_3_transfer.png) | ![](bench_test/results/2026-11-21_183314_p3_3_linearity/plot_3_3_isen_cal.png) |

### 3. Why the gain is 8 % low (N2)
**Method:** held at 1.002 A, DMM in mV across each segment of the source path.
**Result:** across R12 (0.2 Ω pair) 50 mV, as designed; J5 to R12 −1 mV; across R5 (5 mΩ shunt) 5.04 mV, as designed;
**R12's lower end to DUT− 11.68 mV**. That leaves **6.6 mΩ of copper in the shared return**, which the loops count
as sense resistance: a predicted 9 % shortfall, against 8 % measured. ISEN measures across R5 alone, so it stays
accurate.

### 4. Minimum operating voltage
**Method:** fixed setpoint (0.5 / 1.0 / 1.9 A), PSU stepped from 3.0 V down to 0.05 V; dropout is where the current
falls below 98 %.
**Result:** 0.25 V at 0.5 A, 0.55 V at 1 A, 1.7 V at 1.9 A. With the gates saturated, every curve follows
V = 0.559 Ω × I.

![](bench_test/results/2026-11-21_183545_p3_4_dropout/plot_3_4_dropout.png)

### 5. Load-step response (P8)
**Method:** firmware square wave between two currents (50 ms half period), at 5 V and 12 V. Triggered on Gate1,
16× averaged.
**Result:** stable, with no ringing. 0.46 ↔ 1.39 A: 11 to 13 µs (10-90 %), ≤ 1.5 % overshoot. 0.1 ↔ 1.8 A:
11.6 µs rise, 15.7 µs fall.

| Scope: 0.46 → 1.39 A at 5 V (labelled) | Logged data: all four edges at 5 and 12 V |
|---|---|
| ![Load step 0.46 to 1.39 A, labelled scope capture](bench_test/scope_annotated/182042_p3_5_step_5V__step_rise_20us.png) | ![Step response 0.46 to 1.39 A, Python plot](bench_test/results/_combined/plot_3_5_step_20us.png) |
| **Scope: 0.1 → 1.8 A at 5 V (labelled)** | **Logged data: the large step** |
| ![Load step 0.1 to 1.8 A, labelled scope capture](bench_test/scope_annotated/202523_p3_5_step_5V__step_rise_20us.png) | ![Step response 0.1 to 1.8 A, Python plot](bench_test/results/_combined/plot_3_5_step_36-712_20us.png) |

### 6. Hot plug and current-limit recovery (P7)
**Method:** load armed at 0.94 A, then (a) PSU output switched off → on, and (b) PSU current limit raised from
0.5 A to 2.0 A. Single shots.
**Result:** the gates do wind up to the rail with no current, but **no current spike** appears: the KA3005P ramps
over about 1 ms, and the loop tracks it. A stiff source (a battery) was not tested.

| Scope: (a) PSU off → on (labelled) | Logged data, same capture |
|---|---|
| ![Hot plug, labelled scope capture](bench_test/scope_annotated/182814_p3_6_hotplug__hotplug_a_hotplug_1000us.png) | ![Hot plug, Python plot](bench_test/results/2026-11-21_182814_p3_6_hotplug/plot_3_6_a_hotplug_1000us.png) |
| **Scope: (b) current limit 0.5 → 2 A (labelled)** | **Logged data, same capture** |
| ![Current-limit recovery, labelled scope capture](bench_test/scope_annotated/182814_p3_6_hotplug__hotplug_b_recover_1000us.png) | ![Current-limit recovery, Python plot](bench_test/results/2026-11-21_182814_p3_6_hotplug/plot_3_6_b_recover_1000us.png) |

### 7. Thermal limit, no fan (P3)
**Method:** 1.5 A at 10, 20 and 30 W for 3 min each, then a hot ceiling sweep and a cool-down. Thermocouple on the
heatsink, filmed; gate voltage used as a junction thermometer.
**Result:** heatsink **0.99 K/W** (0.12 K/W @ 17 s + 0.87 K/W @ 9.2 min, 0.20 K rms fit over 96 points); die to
heatsink ≈ 3.9 K/W per FET. **≈ 36 W continuous** for Tj ≤ 125 °C. The ceiling rises to 2.04 A when hot.

![](bench_test/results/2026-11-21_202620_p3_8_thermal/plot_3_8_thermal.png)

### 8. Voltage readback (P2)
**Method:** load off, PSU stepped from 0 to 20 V and back; DUT_VSEN measured on the scope and at the MCU.
**Result:** stuck at **3.98 V** at every voltage, because the difference amplifier's feedback resistor goes to the
+ input. Swapping R1 and R4 would give 0.1 × V_DUT. The latched output also holds the MCU's ADC pin above its
3.6 V absolute maximum.

| Scope: DUT at 20 V (labelled) | Logged data, 0 → 20 V and back |
|---|---|
| ![VSEN stuck at 3.96 V, labelled scope capture](bench_test/scope_annotated/185734_p3_7_vsen__vsen_up_20V.png) | ![VSEN against DUT voltage, Python plot](bench_test/results/2026-11-21_185734_p3_7_vsen/plot_3_7_vsen.png) |

### 9. Reference and rails
**Result:** the 4.096 V reference reads 4.093 V on USB power (3 mV low) and moves only +1.7 mV/A under load.
The DAC chain is 1.003 mV/code, with a divider ratio of 0.151 (0.150 designed). The DAC's EEPROM was found holding
2048, which asks for about 5.6 A at power-up (C5); it was zeroed, and the firmware now checks it at boot.

| | |
|---|---|
| ![](bench_test/results/2026-11-21_185831_p3_9_ref/plot_3_9_reference.png) | ![](bench_test/results/2026-11-21_173942_p2_dac_transfer/plot_dac_transfer.png) |
| **Scope: rails on USB power (labelled)** | **Scope: DAC chain at code 1467, gates at the 4.094 V ceiling (labelled)** |
| ![Control-board rails on USB power, labelled scope capture](bench_test/scope_annotated/172940_p1_rails_usb_only__rails_usb_only_dc.png) | ![DAC chain at code 1467, labelled scope capture](bench_test/scope_annotated/173942_p2_dac_transfer__dac_1467.png) |

## Findings

| ID | Status | Finding |
|---|---|---|
| P1 | confirmed | Gate drive capped at the 4.096 V reference → 1.93 A maximum |
| P2 | confirmed | VSEN amplifier has positive feedback and latches high |
| P3 | quantified | ≈ 36 W continuous without a fan |
| P6 | confirmed | MCU ADC pin held above its absolute maximum by the latched VSEN |
| P7 | partly | Gates wind up with no current; no spike from a slow source |
| P8 | confirmed | Loop stable, ≤ 1.5 % overshoot |
| C5 | confirmed | DAC EEPROM held 2048 (≈ 5.6 A at power-up) |
| N1 | new | +5 V rail back-fed to 4.12 V from USB |
| N2 | new, root cause | −8 % setpoint gain from 6.6 mΩ of shared return copper |
| N3 | new | 5 mA at DAC code 0 |
| N4 | new | 4.096 V reference 3 mV low on USB power |

What changes in the next board: [`RevB/`](RevB/README.md).

## Using it

![GUI](bench_test/photos/gui_screenshot.png)

Flash the SAFE firmware ([`firmware/`](firmware/README.md)), then run the GUI ([`gui/`](gui/README.md)). Type the
DUT voltage (Rev A cannot measure it), set the current, and press LOAD ON. The 1.90 A, 30 V and 35 W limits and the
thermal budget are enforced in the firmware as well as in the GUI.
