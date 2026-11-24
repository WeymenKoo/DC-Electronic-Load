# Rev A DC load: control GUI

Minimal Tkinter + pyserial app to run the Rev A load from Windows or macOS, with the limits measured on the bench
(2026-11-21, see [`../bench_test/`](../bench_test/README.md)). Works with the SAFE firmware (`../firmware`), which enforces the same limits in the load itself.

| Start | |
|---|---|
| Windows | double-click `run_gui_windows.bat` (Python 3 from python.org) |
| macOS | double-click `run_gui_mac.command` (Python 3 from python.org; CH340 driver is built into macOS 11+) |
| Any | `pip install pyserial` then `python3 eload_gui.py` |

Use: pick the port marked *CH340*, **Connect**, type the **DUT voltage** (Rev A cannot measure it: VSEN is broken,
finding P2), set the current with the slider or the box (Enter applies), **LOAD ON** / **OFF**.

## Limits built in

| Limit | Value | Source |
|---|---|---|
| Current | ≤ 1.90 A | gate-drive ceiling 1.93 A cold (P1) |
| DUT voltage (typed in) | ≤ 30 V | bench range |
| Power | ≤ 35 W (typed V × setpoint before ON; typed V × measured I live) | |
| Thermal (no sensor) | fitted model of heatsink + die, cut off at the state of **30 W for 3 min from cold**; locked until it cools to 60 % | run `202620` |
| Firmware | tier cap above the setpoint, OCP = setpoint + 0.3 A, 2 s heartbeat (GUI polls every 0.3 s) | test firmware |

What the thermal budget means in practice: ≤ 23 W runs indefinitely, 25 W ≈ 15 min, 30 W 3 min, 35 W a few
seconds (the die heats almost instantly). Closing the window, unplugging USB or a crash switches the load off
(firmware heartbeat). The setpoint uses the measured calibration (2.516 mA/code + 10.5 mA); the displayed current
is ISEN (311.03 counts/A). Below about 0.56 Ω × I the DUT voltage is too low for the requested current (the GUI warns).
