# Rev B: plan for the next board

Rev A was built and measured ([results](../README.md)). This is what Rev B has to change, finding by finding.
Nothing here is built yet.

## Targets

| | Rev A (measured) | Rev B (target) |
|---|---|---|
| Current | 1.93 A (gate-limited) | 10 A |
| Power | ≈ 36 W, no fan | 150 W with a fan |
| DUT voltage | tested to 20 V | 0.6 to 58 V |
| Setpoint accuracy | −8 % uncalibrated | ±1 % after a 2-point calibration |
| Voltage readback | not working | 4-wire, ±0.5 % |
| Protection | firmware only | hardware OTP, OVP, dropout, OFF |

## Finding → change

| Finding (Rev A) | Change in Rev B |
|---|---|
| **P1** gate drive capped at the 4.096 V reference → 1.93 A max | Op-amps on a 12 V rail (≥ 10 V of gate drive); four TO-247 MOSFETs instead of two TO-220 |
| **P2** VSEN difference amp wired with positive feedback (latches high) | Correct difference-amp topology, true 4-wire sense, into an external ADC |
| **P3** ≈ 36 W continuous without a fan | Four MOSFETs on the heat-pipe tower with a blower; NTC on the heatsink read by the ADC |
| **P5 / N2** −8 % setpoint gain from 6.6 mΩ of copper in the shared 5 mΩ shunt return | No shared shunt: each loop senses its own source resistors with Kelvin traces; current readback = mean of the source-resistor voltages |
| **P6** analog outputs drive MCU pins above their absolute maximum | External ADC; series resistor + clamp on anything that reaches the MCU |
| **P7** gates wind up to the rail with no current | Dropout comparator pulls every loop down (anti-wind-up); hardware OFF line, default OFF when USB is absent |
| **P8** loop stable, ≤ 1.5 % overshoot | Keep per-FET loops; re-check compensation for the larger MOSFETs |
| **C1 / C2** fan never turns on; MCU cannot read temperature | Fan driven from the MCU; over-temperature comparator in hardware |
| **C5** DAC powers up at its EEPROM value (found 2048 ≈ 5.6 A) | DAC with volatile registers (powers up at 0) plus the hardware OFF line |
| **N1** +5 V back-fed from USB | Single, isolated power tree from USB-C |
| **N3** 5 mA at code 0 | Small deliberate offset so code 0 is truly 0 A |
| **N4** 4.096 V reference 3 mV low on USB power | DAC/ADC with their own references, supplied from a regulated rail |
| Hot plug from a stiff source (untested) | Covered by the dropout anti-wind-up; test with a battery on Rev B |

Measured Rev A numbers behind each row: [`../bench_test/REPORT.md`](../bench_test/REPORT.md).
