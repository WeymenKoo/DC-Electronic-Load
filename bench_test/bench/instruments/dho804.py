"""Rigol DHO804 over USBTMC (NI-VISA). Commands below were each verified by query on fw 00.01.05."""
from __future__ import annotations

import threading
import time
from pathlib import Path

import numpy as np
import pyvisa

_125 = np.array([1, 2, 5])
SCALES = np.concatenate([_125 * 10.0 ** e for e in range(-3, 2)])   # 1 mV .. 50 V per div (at the probe tip)
DIVS = 8


def nice_scale(span: float) -> float:
    """Smallest 1-2-5 V/div that fits `span` volts in ~7 of the 8 divisions."""
    need = span / 7.0
    for s in SCALES:
        if s >= need:
            return float(s)
    return float(SCALES[-1])


class DHO804:
    def __init__(self, resource: str, rm: pyvisa.ResourceManager | None = None):
        self.rm = rm or pyvisa.ResourceManager()
        self.inst = self.rm.open_resource(resource, timeout=10000)
        self.inst.chunk_size = 1024 * 1024
        self.lock = threading.RLock()

    def close(self):
        try:
            self.inst.close()
        except Exception:
            pass

    def w(self, cmd: str):
        with self.lock:
            self.inst.write(cmd)

    def q(self, cmd: str) -> str:
        with self.lock:
            return self.inst.query(cmd).strip()

    def idn(self) -> str: return self.q("*IDN?")

    def err(self) -> str: return self.q(":SYSTem:ERRor?")

    # ---------------------------------------------------------------- setup
    def channel(self, n: int, on: bool = True, vmin: float = 0.0, vmax: float = 5.0, probe: int = 10,
                bw: str = "20M", coupling: str = "DC"):
        """Display channel n with a 1-2-5 scale/offset so [vmin, vmax] fills the screen."""
        with self.lock:
            self.w(f":CHANnel{n}:DISPlay {1 if on else 0}")
            if not on:
                return
            self.w(f":CHANnel{n}:PROBe {probe}")
            self.w(f":CHANnel{n}:COUPling {coupling}")
            self.w(f":CHANnel{n}:BWLimit {bw}")
            # Offset stays at 0 V: the DHO804 offset error is ~0.1 div + 1 % of the offset, which made large
            # offsets read hundreds of mV wrong. The signal goes in one half of the screen (4 div) instead.
            peak = 1.1 * max(abs(vmin), abs(vmax), 1e-3)
            s = nice_scale(peak * 7.0 / 3.8)
            self.w(f":CHANnel{n}:SCALe {s:g}")
            self.w(f":CHANnel{n}:OFFSet 0")

    def timebase(self, s_per_div: float, offset: float = 0.0):
        self.w(f":TIMebase:MAIN:SCALe {s_per_div:g}")
        self.w(f":TIMebase:MAIN:OFFSet {offset:g}")

    def memdepth(self, pts: int | str):
        self.w(f":ACQuire:MDEPth {pts}")

    def run_auto(self):
        self.w(":TRIGger:SWEep AUTO")
        self.w(":RUN")

    def edge_trigger(self, src: int | str, level: float, slope: str = "POSitive", sweep: str = "SINGle"):
        s = f"CHANnel{src}" if isinstance(src, int) else src
        self.w(":TRIGger:MODE EDGE")
        self.w(f":TRIGger:EDGE:SOURce {s}")
        self.w(f":TRIGger:EDGE:SLOPe {slope}")
        self.w(f":TRIGger:EDGE:LEVel {level:g}")
        self.w(f":TRIGger:SWEep {sweep}")

    def single(self):
        self.w(":SINGle")

    def wait_armed(self, timeout: float = 5.0):
        t = time.time()
        while time.time() - t < timeout:
            if self.q(":TRIGger:STATus?") in ("WAIT", "RUN"):
                return
            time.sleep(0.05)
        raise TimeoutError("scope did not arm")

    def wait_stop(self, timeout: float = 10.0) -> bool:
        t = time.time()
        while time.time() - t < timeout:
            if self.q(":TRIGger:STATus?") == "STOP":
                return True
            time.sleep(0.1)
        return False

    # ---------------------------------------------------------------- measurement
    def item(self, item: str, ch: int, tries: int = 3) -> float:
        """One automatic measurement; 9.9E37 (invalid, e.g. clipped) is retried, then returned as NaN."""
        for _ in range(tries):
            v = float(self.q(f":MEASure:ITEM? {item},CHANnel{ch}"))
            if abs(v) < 9e36:
                return v
            time.sleep(0.3)
        return float("nan")

    def dc(self, chs=(1, 2, 3, 4), items=("VAVG",)) -> dict:
        out = {}
        for c in chs:
            for it in items:
                out[f"ch{c}_{it.lower()}"] = self.item(it, c)
        return out

    def screenshot(self, path: Path):
        with self.lock:
            png = self.inst.query_binary_values(":DISPlay:DATA? PNG", datatype="B", container=bytes)
        Path(path).write_bytes(png)

    def waveform(self, ch: int, raw: bool = True) -> tuple[np.ndarray, np.ndarray, dict]:
        """Time and volts for one channel. raw=True reads the full memory (scope must be stopped)."""
        with self.lock:
            self.w(f":WAVeform:SOURce CHANnel{ch}")
            self.w(f":WAVeform:MODE {'RAW' if raw else 'NORMal'}")
            self.w(":WAVeform:FORMat WORD")
            pre = [float(x) for x in self.q(":WAVeform:PREamble?").split(",")]
            npts = int(pre[2])
            chunk = 250_000
            data = []
            for start in range(1, npts + 1, chunk):
                stop = min(start + chunk - 1, npts)
                self.w(f":WAVeform:STARt {start}")
                self.w(f":WAVeform:STOP {stop}")
                data.append(np.array(self.inst.query_binary_values(":WAVeform:DATA?", datatype="H",
                                                                     is_big_endian=False), dtype=float))
            raw_y = np.concatenate(data) if data else np.zeros(0)
        _, _, _, _, xinc, xorg, xref, yinc, yorg, yref = pre
        t = (np.arange(raw_y.size) - xref) * xinc + xorg
        v = (raw_y - yorg - yref) * yinc
        meta = dict(zip(["format", "type", "points", "count", "xinc", "xorigin", "xref", "yinc", "yorigin", "yref"], pre))
        return t, v, meta
