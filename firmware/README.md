# Rev A firmware (STM32F103RET6)

Bare HAL + register-level firmware for the Rev A control board, built with CMake + Arm GNU and flashed through an
ST-Link on header P2 (SWDIO, SWCLK, GND only). Source: `Core/Src/main.c`. `bringup_2024/` is the original
STM32CubeIDE bring-up project (DAC sweep), kept for reference.

## Two builds

| Build | Output | Use |
|---|---|---|
| **SAFE** (default) | `build/eload_reva.elf`, IDN `ELOAD-REVA-SAFE` | normal use: the measured ceilings are enforced in the firmware, whatever the host sends |
| TEST (`-DBENCH_BUILD=ON`) | `build-bench/eload_reva.elf`, IDN `ELOAD-REVA-TEST` | the permissive firmware used for the bench characterisation (DAC up to 4 A requested, no VDUT / power / thermal checks) |

## Safety ceilings (SAFE build)

| Limit | Value | How |
|---|---|---|
| Current | ≤ 1.90 A (DAC code ≤ 751) | gate-drive ceiling measured at 1.93 A |
| DUT voltage | must be told with `VDUT` before `ARM`; ≤ 30 V | VSEN does not work on Rev A |
| Power | ≤ 35 W: VDUT × setpoint checked on every `ISET`/`DAC`/`STEP`/`VDUT`; VDUT × measured current trips at 36.75 W | |
| Thermal | fitted model (heatsink 0.124 K/W @ 17 s + 0.865 K/W @ 555 s, die 1.97 K/W) cuts off at the state of **30 W for 3 min from cold**; re-arm below 60 % | ≈ 23 W runs indefinitely, 25 W ≈ 15.7 min, 30 W 3:00, 35 W ≈ 4 s |
| Over-current | ISEN > setpoint + 300 mA for 3 ms → off, latched | 1 kHz check |
| Heartbeat | no command for 2 s while armed → off, latched (`WDT`) | the host polls `MEAS?` |
| Watchdog | IWDG 500 ms; reset zeroes the DAC | |
| Power-up | DAC written to 0 before the clock switch; MCP4725 EEPROM rewritten to 0 if it is not | finding C5 |
| After a reset that is not a power-on | thermal state assumed hot (locked ≈ 5 min); `THERM:COLD` clears it when the heatsink is cold | |

## Commands (ASCII, `\n`, 115200 8N1, one-line reply)

| Command | Reply / action |
|---|---|
| `*IDN?` | `ELOAD-REVA-SAFE,<version>,<build date>` |
| `LIMITS?` | the ceilings above |
| `VDUT <mV>` / `VDUT?` | DUT voltage used for the power and thermal limits |
| `ARM` / `DISARM` | arm (refused with a latched fault, no VDUT, or thermal lock) / DAC 0 |
| `ISET <mA>` | set the current; picks the tier and OCP itself. `ERR CAP`, `ERR POWER`, `ERR DISARMED` |
| `DAC <code>` / `DAC?` | raw DAC code (capped) |
| `STEP <a> <b> <half_ms> <cycles>` | square wave between two codes; PC8 (P1 pin 2) high at `b` (scope trigger) |
| `MEAS?` | `isen,vsen,ntc,ntc2,vrefint,tmcu` (averaged ADC counts) `,dac,<A|D><flags hex>,i_mA,p_mW,thermal_%` |
| `STATE?` | `ARMED|DISARMED,<faults>[,THERMAL_LOCK][,EEPROM_ZEROED_AT_BOOT]` |
| `THERM?` | `thermal_%,lock,p_mW,i_mA` |
| `THERM:TEST <mW> <s>` | simulate from the present state: `<% at the end>,<cutoff second or -1>` (no output change) |
| `THERM:COLD` | declare the heatsink cold (disarmed only) |
| `CLEAR` | clear latched faults (stays disarmed) |
| `TIER <n>` / `OCP <counts>` / `OTP <counts>` / `AVG <n>` | lower-level settings used by the bench scripts |
| `EEPROM:ZERO` / `I2C?` | MCP4725 EEPROM write / readback |

Fault flags: 01 WDT, 02 OCP, 04 OTP, 08 I2C, 10 PWR, 20 THERMAL.

## Build and flash (Windows)

Tools: `winget install Arm.ArmGnuToolchain Kitware.CMake Ninja-build.Ninja xpack-dev-tools.openocd-xpack`.

```
.\build.ps1            # both builds
.\flash.ps1 -Probe     # connect, print IDs
.\flash.ps1 -Erase     # mass erase
.\flash.ps1            # program SAFE + verify + reset
.\flash.ps1 -Bench     # program TEST
```

`tools.ps1` puts the toolchain on PATH. `openocd.cfg` uses `transport select swd` and `reset_config none` (no NRST on P2).
