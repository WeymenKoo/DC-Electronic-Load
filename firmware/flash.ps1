# Flash with the ST-Link on P2 (SWDIO, SWCLK, GND only; do NOT connect the ST-Link 3.3 V).
#   .\flash.ps1           program + verify + reset
#   .\flash.ps1 -Erase    mass-erase only (removes the old DAC-sweep firmware)
#   .\flash.ps1 -Probe    connect and print the chip ID only
param([switch]$Erase, [switch]$Probe, [switch]$Bench)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
. .\tools.ps1
if ($Probe) { openocd -f openocd.cfg -c "init; halt; mdw 0xE0042000; mdw 0x1FFFF7E0; reset run; shutdown"; exit $LASTEXITCODE }
if ($Erase) { openocd -f openocd.cfg -c "init; halt; stm32f1x mass_erase 0; reset halt; shutdown"; exit $LASTEXITCODE }
$elf = if ($Bench) { "build-bench/eload_reva.elf" } else { "build/eload_reva.elf" }
openocd -f openocd.cfg -c "program $elf verify reset exit"
