# Puts the winget-installed toolchain on PATH for this PowerShell session. Dot-source it: . .\tools.ps1
$gcc = Get-ChildItem "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\*\bin" -Directory | Select-Object -Last 1
$pk  = "$env:LOCALAPPDATA\Microsoft\WinGet\Packages"
$ocd = Get-ChildItem "$pk\xpack-dev-tools.openocd-xpack_*\*\bin" -Directory | Select-Object -Last 1
$nin = Get-ChildItem "$pk\Ninja-build.Ninja_*" -Directory | Select-Object -Last 1
$env:Path = "$($gcc.FullName);$($ocd.FullName);$($nin.FullName);C:\Program Files\CMake\bin;$env:Path"
