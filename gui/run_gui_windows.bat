@echo off
rem Rev A DC load GUI (Windows). Needs Python 3 from python.org (includes Tkinter).
cd /d "%~dp0"
py -3 -c "import serial" 2>nul || py -3 -m pip install --user pyserial
start "" pyw -3 eload_gui.py
