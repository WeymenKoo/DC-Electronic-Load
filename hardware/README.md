# Rev A hardware (built Nov 2024)

Two Altium boards, stacked on brass standoffs through a 2 × 8 header (P3 on the control board onto P1 on the power
board), on a heat-pipe tower heatsink.

| Folder | Board | Contents |
|---|---|---|
| `control_board/` | Control board: STM32F103RET6, MCP4725 DAC on a REF5040 4.096 V reference, CH340E USB-UART on USB-C, TPS5430 + AMS1117 supplies, LM311 fan comparator | `control_board_schematic.pdf`, Altium project (`.PrjPcb`, `.SchDoc`, `.PcbDoc`), gerbers and BOM in `Project Outputs …/` |
| `power_board/` | Power board: 2 × IRF530 (TO-220), one LMV358 loop per FET, 0.2 Ω ∥ 0.2 Ω source resistors, 5 mΩ shunt with INA240A2, VSEN amplifier, 10 A blade fuse | `power_board_schematic.pdf`, Altium project, gerbers in `Project Outputs …/` |
| `images/` | Both boards | Rendered schematic pages used in the README |
| `netlists/` | Both boards | Netlists extracted from the schematics (`netlist_RevA_*.txt`) |

Header pinout (identical on both boards): 1, 2, 7, 8, 11, 12, 15 GND; 3 +3V3; 4, 13 +4.096V; 5 DUT_ISEN; 6 LOAD_CTRL;
9 TEMP_IN+; 10 DUT_VSEN; 14 FAN−; 16 +12V.

Known faults of this revision and their fixes: [`../README.md`](../README.md#findings), [`../RevB/README.md`](../RevB/README.md).
