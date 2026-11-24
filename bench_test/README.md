# Rev A DC load: measured limits (bench, 2026-11-21)

Documentation of the Nov 2024 Rev A boards **as built**: no bodges were made. Every number below comes from a run
folder in `results/` (raw CSV, `meta.json`, scope PNGs, waveform CSVs).

## 1. Setup

| Item | Value |
|---|---|
| Boards | Control "DC Electronic Load Redemption Edition" (STM32F103RET6) stacked directly on the power board (P3 ↔ P1, brass standoffs, no ribbon). Header pin 10 (DUT_VSEN) left connected |
| Firmware | `ELOAD-REVA-TEST,0.1.0` (`firmware/`), built with Arm GNU 12.2 + CMake/Ninja, flashed via ST-Link V2 + OpenOCD after a mass erase |
| Control power | USB-C on J3 only (no 12 V on J13 in any Phase 3 test) |
| DUT / source | KORAD KA3005P V1.5 (SN 01639604), PSU readback is the current reference |
| Scope | RIGOL DHO804 fw 00.01.05 (USBTMC), 10× probes, grounds on DUT− |
| DMM | +3V3 3.311 V, +4.096V 4.093 V (USB power) |
| Fuse XF1 | 10 A blade (schematic: 5 A). Never the limit |
| Cooling | Heatsink (isolated tabs, heatsink at GND), **no fan** |
| Temperature | Ambient 20 °C (board at 23.7 °C at the start of 3.8). No NTC readout (bead on the heatsink, not wired to the MCU). Brymen BM235 K-type TC: first on Q2's package top (23.8 → 29.0 °C during the 12 V step test), then for 3.8 pressed against the heatsink at the pink thermal pad next to Q2, held by the bent, tensioned wire (photo `IMG_0196`: the angle makes it look like the PCB; confirmed by the operator), video `IMG_0197` |
| E-stop | KA3005P OUTPUT button, or pulling the USB-C (powers the whole load down). Neither depends on firmware; a hardware OFF is a Rev B item |

**Scope calibration:** probe check with all four tips on J10 (`results/*_p0_probecheck`). CH2/3/4 read
−0.46/−0.77/−1.01 % and are gain-corrected in all plots. CH1 has an offset error (−0.11 to −0.24 V depending on
V/div), so CH1 data are slope-fitted, zero-referenced or used relative only. DC absolute values come from the DMM
and the PSU.

## 2. Measured limits

| Quantity | Measured | Prediction (design review) | Run |
|---|---|---|---|
| **Max current, gate-limited** | **1.933 A total** (≈ 0.97 A per FET); gates pinned at 4.07 V | 0.04 to 5.7 A per FET (Vth lottery) | `181147` tier 4 |
| Max current, fuse-limited | not reached (10 A fuse; tests stop far below) | 5 A | |
| Gate-drive ceiling | 4.094 V (= the +4.096V rail) | about 4.0 V | `173942` |
| Setpoint transfer | I = 2.516 mA/code + 10.5 mA, ±6.8 mA over 0 to 1.83 A (nominal 2.727 mA/code: −8 %) | 2.727 mA/code | `183314` |
| Zero-code current | 5.0 mA at DAC 0 (3 V applied) | 0 | `183314` |
| Dead zone | none (current follows from code 2) | | `183314` |
| ISEN (MCU readback) | 311.03 counts/A, −0.61 counts offset; within 0.6 % of design, ±10 mA of the PSU to 1.9 A | 310 counts/A | `183314` |
| Current sharing Q1/Q2 | +0.1 % imbalance at 0.94 A; equal to the gate ceiling | | `180150`..`181147` |
| Minimum DUT voltage (98 %) | 0.25 V at 0.5 A, **0.55 V at 1.0 A**, 1.70 V at 1.89 A (lead drop included) | 0.3 to 0.5 V at 1 A | `183545` |
| Full-on path resistance | 0.559 Ω (gates saturated: FETs at VGS ≈ 4 V + source R + shunt + fuse + leads) | | `183545` |
| Step response 0.46 ↔ 1.39 A | 10-90 % 11-13 µs, overshoot ≤ 1.5 %, settles to ±5 % in 12-16 µs; same at 5 V and 12 V | stable | `182042`, `182124` |
| Hot plug / CC recovery (KA3005P) | no spike: current ramps with the PSU (~1 ms); peak within the noise of the final 0.94 A | > setpoint | `182814` |
| VSEN | 3.98 V at every DUT voltage 0-20 V (latched); PA2 pinned at 4095 | stuck near a rail | `185623`, `185734` |
| +4.096 V under load | +1.72 mV/A vs DUT−, +3.3 mV at 1.89 A (+0.08 %), reversible | may drift | `185831` |
| +4.096 V on USB only | 4.093 V (−3 mV, outside ±2 mV) | may sag on USB | `172940` |
| MCP4725 power-up value | EEPROM held **2048** (= 2.048 V, ≈ 5.6 A requested at power-up); zeroed | whatever is in EEPROM | Phase 2 |
| **Continuous power without fan** | **≈ 36 W** for Tj ≤ 125 °C at 25 °C ambient (band 29-48 W); Tj ≈ 109 °C at 30 W steady state (87-129 °C) | 20 to 30 W | `202620` |
| Hot gate-limited ceiling | **2.039 A** straight after 3 min at 30 W (+5.5 % vs cold) | | `202620` |
| Large step 0.10 ↔ 1.80 A | rise 11.6-11.8 µs, fall 15.7-16.2 µs (10-90 %), overshoot ≤ 0.6 %, settle ±5 % in 12 µs / 21-23 µs | | `202523`, `202551` |
| **Heatsink to ambient** (TC on the heatsink at Q2's thermal pad) | 0.12 K/W @ τ 17 s (pad area next to the tab) + 0.87 K/W @ τ 9.2 min (heatsink mass) = **0.99 K/W** per W of load power (fit rms 0.20 K, 96 points) | | `202620` |
| Die to heatsink | ≈ 3.9 K/W per FET (RθJC 1.7 + ≈ 2.2 K/W thermal pad and mounting), from the gate-voltage proxy | | `202620` |

## 3. Findings

| ID | Status | Finding | Evidence |
|---|---|---|---|
| **P1** | **confirmed** | Gate drive capped at the 4.096 V rail: 4.094 V with no current, 4.07 V at the knee. The load cannot exceed **1.93 A** at 3 V and 20 °C ambient, and above 0.5 A its minimum DUT voltage rises steeply (0.56 Ω full-on) | `plot_3_1_*`, `plot_gate_ceiling`, `plot_3_4_dropout` |
| **P2** | **confirmed** | VSEN latched at 3.98 V regardless of DUT voltage, before any voltage is applied | `plot_3_7_vsen` |
| **P3** | **confirmed (quantified)** | Two IRF530 on this heatsink without a fan: ≈ 36 W continuous (29-48 W). Method: K-type TC on the heatsink at Q2's thermal pad (filmed, 90 frames read, synced to the PC log via 4 PSU-display transitions, ±1.5 s), fitted with a two-pole model; die temperature from the gate voltage at constant current (VGS drop), its tempco self-calibrated at −3.7 mV/K by matching the slow pole. Limits: one TC point; the run was 23 min, so a pole slower than ~30 min would not be seen | `plot_3_8_thermal` |
| P4 | measured, minor | Reference moves +1.72 mV/A (+0.08 % at 1.9 A) including ground drop: negligible at these currents | `plot_3_9_reference` |
| P5 | consistent | Each loop regulates its source node (0.1 Ω·I_ch + 5 mΩ·I_total); sharing is equal, see N2 for the gain | `plot_3_1_sharing` |
| **P6** | **confirmed** | PA2 is driven above its 3.6 V absolute maximum (DUT_VSEN 3.98 V, no series resistor) whenever the board is powered. PA1 (ISEN) still calibrates within 0.6 % | `p3_7` data |
| P7 | partly confirmed / open | Wind-up confirmed (gates sit at the rail with no current). With the KA3005P (ms ramps) no current spike appears; a stiff source applied in µs is untested | `plot_3_6_*` |
| **P8** | **confirmed (stable)** | No ringing, ≤ 1.5 % overshoot, 11-13 µs 10-90 % with IRF530 | `plot_3_5_step_*` |
| P9 | confirmed | NTC on TEMP_IN+ is fed from 12 V via 470 Ω and never reaches the MCU (traced in the netlist) | session notes |
| C1 | not tested | Fan cannot turn on (no fan fitted) | |
| C2 | confirmed | MCU cannot read temperature (same trace as P9) | |
| C4 | measured | VDDA 3.311 V (DMM); VREFINT estimate 3.294 V | Phase 1/2 |
| **C5** | **confirmed** | MCP4725 EEPROM held 2048: at power-up, before the MCU runs, the load requested ≈ 5.6 A (gate-limited to ~1.9 A). Zeroed during this session | Phase 2 |
| **N1** | **new** | With USB only, the TPS5430 output (+5V) sits at 4.12 V: back-fed from USB, consistent with Q3's body diode | `172940` |
| **N2** | **new, root cause found** | Setpoint gain −8 % (2.516 vs 2.727 mA/code). DMM survey at 1.002 A (run `211155`): R12 50 mV (0.2 Ω pairs exact), J5→R12 −1 mV, **Rshunt node→DUT− 11.68 mV** of which R5 = 5.04 mV: **6.6 mΩ of copper in the shared return** (Rshunt trace, ground pour to DUT−). The loop regulates 0.1 Ω·I/2 + R_return·I: the fitted gain implies ≈ 10 mΩ seen by the loop (its ground tap sits ≈ 1.6 mΩ above DUT−). This is P5 made worse by layout. ISEN (INA240 across R5 only) is unaffected, which is why ISEN agrees with the PSU | `dmm_readings.csv` |
| **N3** | **new** | 5 mA flows at DAC code 0 (LMV358 offsets); the loops sit at their own input offsets with gates at 2.6-2.7 V | `183314` |
| **N4** | **new** | +4.096 V is 3 mV low on USB power alone | `172940` |

**Unexplained** (kept, not toleranced):
1. 3.7 run `185623`: one VSEN reading of 0.08-0.22 V (whole capture) at 7.5 V on the up-sweep, not reproduced in
   `185734`; both runs show isolated VMIN dips to 2.4-2.7 V. **Resolved by analysis: measurement artefact.** In the
   as-built stage (R1 from OUT B to +IN B, R4 from −IN B to GND) +IN = (2k·V_DUT + 20k·V_OUT)/22k while −IN = 0 V,
   so for any V_DUT ≥ 0 the output can only latch high; at 7.5 V even a low output gives +IN = 0.68 V > 0. The low
   readings were the CH2 clip on header pin P3.10 losing contact. Fix: swap R1 and R4 → V_OUT = 0.1 × V_DUT.
2. ~~Control board lost USB power while probes were moved.~~ **Resolved:** the operator had not re-plugged the USB-C.
   Not a board fault. Recovered with no change of state (DISARMED, EEPROM 0).

## 4. Next revision

Each finding and the change it drives in the next board: [`../RevB/README.md`](../RevB/README.md).

## 5. Plots

Combined (`results/_combined/`):

| | |
|---|---|
| Requested vs delivered, knee at 1.93 A | ![](results/_combined/plot_3_1_req_vs_delivered.png) |
| Gate voltage vs current, ceiling | ![](results/_combined/plot_3_1_gate_vs_current.png) |
| Current sharing Q1/Q2 | ![](results/_combined/plot_3_1_sharing.png) |
| ISEN vs PSU | ![](results/_combined/plot_3_1_isen_error.png) |
| Step response, 20 µs/div | ![](results/_combined/plot_3_5_step_20us.png) |
| Step response, 200 µs/div | ![](results/_combined/plot_3_5_step_200us.png) |

Per run:

| | |
|---|---|
| Probe check | ![](results/2026-11-21_173245_p0_probecheck/plot_probecheck.png) |
| Rail noise (USB only) | ![](results/2026-11-21_172940_p1_rails_usb_only/plot_ripple.png) |
| DAC chain transfer | ![](results/2026-11-21_173942_p2_dac_transfer/plot_dac_transfer.png) |
| Divider ratio | ![](results/2026-11-21_173942_p2_dac_transfer/plot_divider_ratio.png) |
| Gate ceiling, no DUT | ![](results/2026-11-21_173942_p2_dac_transfer/plot_gate_ceiling.png) |
| Setpoint transfer + residual | ![](results/2026-11-21_183314_p3_3_linearity/plot_3_3_transfer.png) |
| ISEN calibration | ![](results/2026-11-21_183314_p3_3_linearity/plot_3_3_isen_cal.png) |
| Dropout | ![](results/2026-11-21_183545_p3_4_dropout/plot_3_4_dropout.png) |
| Hot plug (a) | ![](results/2026-11-21_182814_p3_6_hotplug/plot_3_6_a_hotplug_1000us.png) |
| CC recovery (b) | ![](results/2026-11-21_182814_p3_6_hotplug/plot_3_6_b_recover_1000us.png) |
| VSEN latch | ![](results/2026-11-21_185734_p3_7_vsen/plot_3_7_vsen.png) |
| Reference under load | ![](results/2026-11-21_185831_p3_9_ref/plot_3_9_reference.png) |
| Thermal run + fit + gate proxy | ![](results/2026-11-21_202620_p3_8_thermal/plot_3_8_thermal.png) |
| **Operating envelope (for buck testing)** | ![](results/2026-11-21_202620_p3_8_thermal/plot_operating_envelope.png) |
| Large step 0.1 ↔ 1.8 A, 20 µs/div | ![](results/_combined/plot_3_5_step_36-712_20us.png) |

Scope captures, labelled (`scope_annotated/`; the raw screenshots stay in each run
folder). Superseded runs are kept and say so on the capture:

| Capture | File |
|---|---|
| Control-board rails on USB power only (run 172238) (superseded) | [`172238_p1_rails_usb_only__rails_usb_only_dc.png`](scope_annotated/172238_p1_rails_usb_only__rails_usb_only_dc.png) |
| Rail noise on USB power only (run 172238) (superseded) | [`172238_p1_rails_usb_only__rails_usb_only_ripple.png`](scope_annotated/172238_p1_rails_usb_only__rails_usb_only_ripple.png) |
| Control-board rails on USB power only (run 172348) (superseded) | [`172348_p1_rails_usb_only__rails_usb_only_dc.png`](scope_annotated/172348_p1_rails_usb_only__rails_usb_only_dc.png) |
| Rail noise on USB power only (run 172348) (superseded) | [`172348_p1_rails_usb_only__rails_usb_only_ripple.png`](scope_annotated/172348_p1_rails_usb_only__rails_usb_only_ripple.png) |
| Control-board rails on USB power only (run 172905) (superseded) | [`172905_p1_rails_usb_only__rails_usb_only_dc.png`](scope_annotated/172905_p1_rails_usb_only__rails_usb_only_dc.png) |
| Rail noise on USB power only (run 172905) (superseded) | [`172905_p1_rails_usb_only__rails_usb_only_ripple.png`](scope_annotated/172905_p1_rails_usb_only__rails_usb_only_ripple.png) |
| Control-board rails on USB power only (run 172940) | [`172940_p1_rails_usb_only__rails_usb_only_dc.png`](scope_annotated/172940_p1_rails_usb_only__rails_usb_only_dc.png) |
| Rail noise on USB power only (run 172940) | [`172940_p1_rails_usb_only__rails_usb_only_ripple.png`](scope_annotated/172940_p1_rails_usb_only__rails_usb_only_ripple.png) |
| Probe check: all four probes on the 4.096 V reference | [`173245_p0_probecheck__probecheck.png`](scope_annotated/173245_p0_probecheck__probecheck.png) |
| DAC chain at code 0, no DUT (run 173834) (superseded) | [`173834_p2_dac_transfer__dac_0000.png`](scope_annotated/173834_p2_dac_transfer__dac_0000.png) |
| DAC chain at code 700, no DUT (run 173834) (superseded) | [`173834_p2_dac_transfer__dac_0700.png`](scope_annotated/173834_p2_dac_transfer__dac_0700.png) |
| DAC chain at code 1467, no DUT (run 173834) (superseded) | [`173834_p2_dac_transfer__dac_1467.png`](scope_annotated/173834_p2_dac_transfer__dac_1467.png) |
| DAC chain at code 0, no DUT (run 173942) | [`173942_p2_dac_transfer__dac_0000.png`](scope_annotated/173942_p2_dac_transfer__dac_0000.png) |
| DAC chain at code 700, no DUT (run 173942) | [`173942_p2_dac_transfer__dac_0700.png`](scope_annotated/173942_p2_dac_transfer__dac_0700.png) |
| DAC chain at code 1467, no DUT (run 173942) | [`173942_p2_dac_transfer__dac_1467.png`](scope_annotated/173942_p2_dac_transfer__dac_1467.png) |
| Load step 0.46 ↔ 1.39 A, rising edge, 5 V DUT, 20 µs/div (superseded) | [`181936_p3_5_step_5V__step_rise_20us.png`](scope_annotated/181936_p3_5_step_5V__step_rise_20us.png) |
| Load step 0.46 ↔ 1.39 A, rising edge, 5 V DUT, 20 µs/div | [`182042_p3_5_step_5V__step_rise_20us.png`](scope_annotated/182042_p3_5_step_5V__step_rise_20us.png) |
| Load step 0.46 ↔ 1.39 A, rising edge, 5 V DUT, 200 µs/div | [`182042_p3_5_step_5V__step_rise_200us.png`](scope_annotated/182042_p3_5_step_5V__step_rise_200us.png) |
| Load step 0.46 ↔ 1.39 A, falling edge, 5 V DUT, 20 µs/div | [`182042_p3_5_step_5V__step_fall_20us.png`](scope_annotated/182042_p3_5_step_5V__step_fall_20us.png) |
| Load step 0.46 ↔ 1.39 A, falling edge, 5 V DUT, 200 µs/div | [`182042_p3_5_step_5V__step_fall_200us.png`](scope_annotated/182042_p3_5_step_5V__step_fall_200us.png) |
| Load step 0.46 ↔ 1.39 A, rising edge, 12 V DUT, 20 µs/div | [`182124_p3_5_step_12V__step_rise_20us.png`](scope_annotated/182124_p3_5_step_12V__step_rise_20us.png) |
| Load step 0.46 ↔ 1.39 A, rising edge, 12 V DUT, 200 µs/div | [`182124_p3_5_step_12V__step_rise_200us.png`](scope_annotated/182124_p3_5_step_12V__step_rise_200us.png) |
| Load step 0.46 ↔ 1.39 A, falling edge, 12 V DUT, 20 µs/div | [`182124_p3_5_step_12V__step_fall_20us.png`](scope_annotated/182124_p3_5_step_12V__step_fall_20us.png) |
| Load step 0.46 ↔ 1.39 A, falling edge, 12 V DUT, 200 µs/div | [`182124_p3_5_step_12V__step_fall_200us.png`](scope_annotated/182124_p3_5_step_12V__step_fall_200us.png) |
| Load step 0.1 ↔ 1.8 A, rising edge, 5 V DUT, 20 µs/div | [`202523_p3_5_step_5V__step_rise_20us.png`](scope_annotated/202523_p3_5_step_5V__step_rise_20us.png) |
| Load step 0.1 ↔ 1.8 A, rising edge, 5 V DUT, 200 µs/div | [`202523_p3_5_step_5V__step_rise_200us.png`](scope_annotated/202523_p3_5_step_5V__step_rise_200us.png) |
| Load step 0.1 ↔ 1.8 A, falling edge, 5 V DUT, 20 µs/div | [`202523_p3_5_step_5V__step_fall_20us.png`](scope_annotated/202523_p3_5_step_5V__step_fall_20us.png) |
| Load step 0.1 ↔ 1.8 A, falling edge, 5 V DUT, 200 µs/div | [`202523_p3_5_step_5V__step_fall_200us.png`](scope_annotated/202523_p3_5_step_5V__step_fall_200us.png) |
| Load step 0.1 ↔ 1.8 A, rising edge, 12 V DUT, 20 µs/div | [`202551_p3_5_step_12V__step_rise_20us.png`](scope_annotated/202551_p3_5_step_12V__step_rise_20us.png) |
| Load step 0.1 ↔ 1.8 A, rising edge, 12 V DUT, 200 µs/div | [`202551_p3_5_step_12V__step_rise_200us.png`](scope_annotated/202551_p3_5_step_12V__step_rise_200us.png) |
| Load step 0.1 ↔ 1.8 A, falling edge, 12 V DUT, 20 µs/div | [`202551_p3_5_step_12V__step_fall_20us.png`](scope_annotated/202551_p3_5_step_12V__step_fall_20us.png) |
| Load step 0.1 ↔ 1.8 A, falling edge, 12 V DUT, 200 µs/div | [`202551_p3_5_step_12V__step_fall_200us.png`](scope_annotated/202551_p3_5_step_12V__step_fall_200us.png) |
| (a) PSU output switched off → on, load armed at 0.94 A, 1 ms/div | [`182814_p3_6_hotplug__hotplug_a_hotplug_1000us.png`](scope_annotated/182814_p3_6_hotplug__hotplug_a_hotplug_1000us.png) |
| (a) PSU output switched off → on, load armed at 0.94 A, 50 µs/div | [`182814_p3_6_hotplug__hotplug_a_hotplug_50us.png`](scope_annotated/182814_p3_6_hotplug__hotplug_a_hotplug_50us.png) |
| (b) PSU current limit raised 0.5 → 2.0 A, load armed at 0.94 A, 1 ms/div | [`182814_p3_6_hotplug__hotplug_b_recover_1000us.png`](scope_annotated/182814_p3_6_hotplug__hotplug_b_recover_1000us.png) |
| (b) PSU current limit raised 0.5 → 2.0 A, load armed at 0.94 A, 50 µs/div | [`182814_p3_6_hotplug__hotplug_b_recover_50us.png`](scope_annotated/182814_p3_6_hotplug__hotplug_b_recover_50us.png) |
| Voltage readback stuck, DUT at 20 V (run 185623) (superseded) | [`185623_p3_7_vsen__vsen_up_20V.png`](scope_annotated/185623_p3_7_vsen__vsen_up_20V.png) |
| Voltage readback stuck, DUT at 0 V (run 185623) (superseded) | [`185623_p3_7_vsen__vsen_down_0V.png`](scope_annotated/185623_p3_7_vsen__vsen_down_0V.png) |
| Voltage readback stuck, DUT at 20 V (run 185734) | [`185734_p3_7_vsen__vsen_up_20V.png`](scope_annotated/185734_p3_7_vsen__vsen_up_20V.png) |
| Voltage readback stuck, DUT at 0 V (run 185734) | [`185734_p3_7_vsen__vsen_down_0V.png`](scope_annotated/185734_p3_7_vsen__vsen_down_0V.png) |

**Using this load on buck converters (summary of the envelope):** below ~1 V the 0.56 Ω full-on path limits current
(0.5 A needs ≥ 0.25 V, 1 A ≥ 0.55 V, 1.9 A ≥ 1.7 V); from ~1.7 V to ~15-18 V the gate drive caps it at 1.93 A cold
(2.04 A hot); above that the thermal limit (~36 W, no fan) takes over. Setpoint: use the 3.3 calibration
(2.516 mA/code + 10.5 mA) or read back ISEN (311 counts/A). Load steps up to 1.7 A with 12-16 µs edges and no load
ringing; STEP half-period ≥ 1 ms (I2C DAC update ~0.3 ms).

## 6. Not done

- Phase 1 with 12 V on J13 (12 V only, 12 V + USB) and the TPS5430 switching ripple.
- Tier 5 (4 A): not needed, the gate-limited ceiling is below tier 4's cap.
- Hot plug from a stiff source (battery / capacitor bank).

## Files

| Path | What |
|---|---|
| `bench/instruments/` | Drivers: KORAD KA3005P (serial), RIGOL DHO804 (USBTMC / VISA), the load (serial) |
| `bench/config.py`, `config.toml` | Instrument auto-detection (USB VID:PID), board calibration, scope probe gain factors |
| `results/<date>_<time>_<test>/` | `data.csv`, `meta.json` (instruments, probe map, settings), scope PNGs, `waveforms/*.csv`, plots |
| `results/_combined/` | Plots across runs (current tiers, step response) |
| `scope_annotated/` | Every raw scope screenshot with a title, conditions, channel legend (signal, probe point, scale, offset) and call-outs |
| `photos/` | Bench photos and the GUI screenshot |
