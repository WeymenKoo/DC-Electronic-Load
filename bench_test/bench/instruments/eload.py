"""Rev A load test firmware over the CH340E (115200 8N1, '\\n' terminated, one-line replies)."""
from __future__ import annotations

import threading
import time
from dataclasses import dataclass

import serial

MAX_DAC_CODE = 1467


@dataclass
class Meas:
    t: float
    isen: float
    vsen: float
    ntc: float
    ntc2: float
    vrefint: float
    tmcu: float
    dac: int
    armed: bool
    flags: int

    @property
    def vdda_from_vrefint(self) -> float:
        """VDDA estimate from VREFINT (1.20 V typical, no factory trim on the F103: +/-3 %)."""
        return 1.20 * 4095.0 / self.vrefint if self.vrefint > 0 else float("nan")

    def tmcu_C(self, vdda: float) -> float:
        v = self.tmcu / 4095.0 * vdda
        return (1.43 - v) / 0.0043 + 25.0        # datasheet typicals: V25 1.43 V, 4.3 mV/K


class ELoad:
    def __init__(self, port: str, timeout: float = 1.0):
        self.port = port
        self.ser = serial.Serial(port, 115200, timeout=timeout)
        self.lock = threading.RLock()
        time.sleep(0.2)
        self.ser.reset_input_buffer()

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def query(self, cmd: str, lock_timeout: float = 2.0) -> str:
        if not self.lock.acquire(timeout=lock_timeout):
            raise TimeoutError("load serial lock")
        try:
            self.ser.reset_input_buffer()
            self.ser.write((cmd + "\n").encode())
            line = self.ser.readline().decode(errors="replace").strip()
            if not line:
                raise TimeoutError(f"load: no reply to {cmd!r}")
            return line
        finally:
            self.lock.release()

    def cmd(self, cmd: str) -> str:
        r = self.query(cmd)
        if not r.startswith("OK"):
            raise RuntimeError(f"load: {cmd!r} -> {r!r}")
        return r

    # convenience
    def idn(self) -> str: return self.query("*IDN?")
    def state(self) -> str: return self.query("STATE?")
    def arm(self): self.cmd("ARM")
    def disarm(self): self.cmd("DISARM")
    def clear(self): self.cmd("CLEAR")
    def tier(self, n: int): self.cmd(f"TIER {n}")
    def avg(self, n: int): self.cmd(f"AVG {n}")

    def dac(self, code: int):
        code = int(code)
        if not 0 <= code <= MAX_DAC_CODE:
            raise ValueError(f"DAC code {code} outside 0..{MAX_DAC_CODE}")
        self.cmd(f"DAC {code}")

    def meas(self) -> Meas:
        r = self.query("MEAS?")
        f = r.split(",")
        if len(f) < 8:                       # SAFE firmware appends i_mA, p_mW, thermal %
            raise RuntimeError(f"load: bad MEAS reply {r!r}")
        st = f[7]
        return Meas(time.time(), *(float(x) for x in f[:6]), int(f[6]), st[0] == "A", int(st[1:], 16))
