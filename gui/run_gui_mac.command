#!/bin/bash
# Rev A DC load GUI (macOS). Use Python 3 from python.org (its Tkinter works; the Xcode python3 Tk is outdated).
cd "$(dirname "$0")"
python3 -c "import serial" 2>/dev/null || python3 -m pip install --user pyserial
python3 eload_gui.py
