# Build both firmware variants:
#   build\eload_reva.elf        SAFE (default): measured ceilings enforced in firmware
#   build-bench\eload_reva.elf  TEST: permissive bench-test firmware used for the characterisation
param([switch]$BenchOnly, [switch]$SafeOnly)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
. .\tools.ps1
if (-not $BenchOnly) { cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBENCH_BUILD=OFF; cmake --build build }
if (-not $SafeOnly)  { cmake -S . -B build-bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DBENCH_BUILD=ON; cmake --build build-bench }
