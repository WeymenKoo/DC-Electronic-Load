"""Korad KA3005P over its virtual COM port. Verified on unit 'KORAD KA3005P V1.5': 9600 8N1, no terminator."""
from __future__ import annotations

import threading
import time

import serial

V_MAX_HARD = 30.0
I_MAX_HARD = 4.2


class KA3005P:
    GAP = 0.06   # seconds between commands

    def __init__(self, port: str):
        self.port = port
        self.ser = serial.Serial(port, 9600, timeout=0.05)
        self.lock = threading.RLock()
        self._last = 0.0

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def _gap(self):
        dt = time.time() - self._last
        if dt < self.GAP:
            time.sleep(self.GAP - dt)

    def write(self, cmd: str, lock_timeout: float = 2.0):
        if not self.lock.acquire(timeout=lock_timeout):
            raise TimeoutError("psu lock")
        try:
            self._gap()
            self.ser.write(cmd.encode())
            self._last = time.time()
        finally:
            self.lock.release()

    def query(self, cmd: str, quiet: float = 0.04, total: float = 0.6) -> bytes:
        with self.lock:
            self._gap()
            self.ser.reset_input_buffer()
            self.ser.write(cmd.encode())
            out, t_end, t_last = b"", time.time() + total, None
            while time.time() < t_end:
                b = self.ser.read(64)
                if b:
                    out += b
                    t_last = time.time()
                elif t_last and time.time() - t_last > quiet:
                    break
            self._last = time.time()
            if not out:
                raise TimeoutError(f"psu: no reply to {cmd!r}")
            return out

    def qf(self, cmd: str) -> float:
        return float(self.query(cmd).decode(errors="replace").strip())

    def idn(self) -> str: return self.query("*IDN?").decode(errors="replace").strip()
    def vout(self) -> float: return self.qf("VOUT1?")
    def iout(self) -> float: return self.qf("IOUT1?")
    def vset_q(self) -> float: return self.qf("VSET1?")
    def iset_q(self) -> float: return self.qf("ISET1?")

    def status(self) -> dict:
        b = self.query("STATUS?")[0]
        return {"raw": b, "cv": bool(b & 0x01), "output": bool(b & 0x40)}

    def set_v(self, v: float):
        if not 0.0 <= v <= V_MAX_HARD:
            raise ValueError(f"PSU V {v} outside 0..{V_MAX_HARD}")
        self.write(f"VSET1:{v:.2f}")
        rb = self.vset_q()
        if abs(rb - round(v, 2)) > 0.006:
            raise RuntimeError(f"PSU VSET readback {rb} != {v}")

    def set_i(self, i: float):
        if not 0.0 <= i <= I_MAX_HARD:
            raise ValueError(f"PSU I {i} outside 0..{I_MAX_HARD}")
        self.write(f"ISET1:{i:.3f}")
        rb = self.iset_q()
        if abs(rb - round(i, 3)) > 0.0006:
            raise RuntimeError(f"PSU ISET readback {rb} != {i}")

    def output(self, on: bool):
        self.write("OUT1" if on else "OUT0")
        time.sleep(0.1)
        if self.status()["output"] != on:
            raise RuntimeError(f"PSU output did not switch {'on' if on else 'off'}")
