#!/usr/bin/env python3
"""Rev A DC load: minimal control GUI (Windows and macOS). Needs Python 3.9+ with Tkinter, and pyserial.

    python eload_gui.py            (Windows: run_gui_windows.bat, macOS: run_gui_mac.command)

Talks to the Rev A firmware over the CH340 on J3, 115200 8N1. With the SAFE firmware (ELOAD-REVA-SAFE) the same
limits are also enforced inside the load, so they hold whatever the host does; the GUI checks them first.

Limits are constants below, taken from the 2026-11-21 bench measurements (see ../REPORT.md):
  * current setpoint <= 1.90 A (gate-drive ceiling 1.93 A cold)
  * DUT voltage (entered by you, VSEN on this board is broken) <= 30 V
  * power <= 35 W, from the entered voltage x the measured current
  * thermal budget: there is no temperature sensor, so the GUI runs the fitted thermal model
    (heatsink 0.12 K/W @ 17 s + 0.87 K/W @ 555 s, die 1.97 K/W) on the live power and cuts the load off at the
    state reached by 30 W for 3 min from cold. 30 W cuts off at 3:00, lower power runs longer. After a cutoff the
    load stays locked until the model has cooled to 60 % of the budget.
  * firmware side: tier cap above the setpoint, OCP at setpoint + 0.3 A, 2 s heartbeat (this GUI polls every
    0.3 s; if it closes or hangs, the firmware switches the load off by itself).
"""
from __future__ import annotations

import queue
import threading
import time

import serial
from serial.tools import list_ports

# ----------------------------------------------------------------------------------------------- limits
I_MAX_A = 1.90
V_MAX_V = 30.0
P_MAX_W = 35.0
OCP_MARGIN_A = 0.30
POLL_S = 0.3

# Calibration (run 183314): I = 2.516 mA/code + 10.5 mA; ISEN counts = 311.03 * I - 0.61
MA_PER_CODE = 2.516
OFFSET_MA = 10.5
ISEN_COUNTS_PER_A = 311.03
ISEN_ZERO = -0.61
# Firmware tiers (cap codes, requested-current scale 2.727 mA/code)
TIER_CODES = {1: 184, 2: 367, 3: 734, 4: 1100, 5: 1467}

# Thermal model (run 202620): K above ambient per W of load power
R1, TAU1 = 0.124, 17.2        # heatsink near the tab
R2, TAU2 = 0.865, 554.6       # heatsink mass
R_DIE = 1.97                  # die above heatsink, fast (tau ~4 s, treated as instant)
REARM_FRACTION = 0.60
VDROP_OHM, VDROP_OFF = 0.559, -0.038   # full-on path (3.4): V_min = 0.559 * I - 0.038


def thermal_limit_K() -> float:
    """Model state after 30 W for 180 s from cold."""
    x1 = x2 = 0.0
    for _ in range(1800):
        x1 += 0.1 / TAU1 * (30.0 - x1)
        x2 += 0.1 / TAU2 * (30.0 - x2)
    return R1 * x1 + R2 * x2 + R_DIE * 30.0


def code_for(i_a: float) -> int:
    if i_a <= OFFSET_MA / 1000:
        return 0
    return int(round((i_a * 1000 - OFFSET_MA) / MA_PER_CODE))


def tier_for(code: int) -> int:
    for t, cap in TIER_CODES.items():
        if code <= cap:
            return t
    return 5


class Load:
    """Serial link + limits + thermal model. No UI code, so it can be tested on its own."""

    def __init__(self):
        self.ser: serial.Serial | None = None
        self.lock = threading.Lock()
        self.limit_K = thermal_limit_K()
        self.x1 = self.x2 = 0.0
        self.t_last = time.time()
        self.locked_out = False
        self.on = False
        self.i_set = 0.0
        self.v_dut = None
        self.last = {}

    # serial
    @staticmethod
    def ports():
        out = []
        for p in list_ports.comports():
            tag = " (CH340, load)" if (p.vid, p.pid) == (0x1A86, 0x7523) else ""
            out.append((p.device, p.device + tag))
        out.sort(key=lambda x: "CH340" not in x[1])
        return out

    def connect(self, port: str) -> str:
        self.ser = serial.Serial(port, 115200, timeout=0.5)
        time.sleep(0.2)
        self.ser.reset_input_buffer()
        idn = self.q("*IDN?")
        if not idn.startswith("ELOAD-REVA"):
            self.ser.close(); self.ser = None
            raise RuntimeError(f"unexpected device: {idn!r}")
        self.q("DAC 0"); self.q("DISARM")
        return idn

    def close(self):
        if self.ser:
            try:
                self.q("DAC 0"); self.q("DISARM")
            except Exception:
                pass
            self.ser.close()
        self.ser = None
        self.on = False

    def q(self, cmd: str) -> str:
        with self.lock:
            if not self.ser:
                raise RuntimeError("not connected")
            self.ser.reset_input_buffer()
            self.ser.write((cmd + "\n").encode())
            r = self.ser.readline().decode(errors="replace").strip()
        if not r:
            raise RuntimeError(f"no reply to {cmd}")
        return r

    # control
    def check(self, i_a: float, v_dut: float | None) -> list[str]:
        errs = []
        if v_dut is None:
            errs.append("enter the DUT voltage (VSEN on this board does not work)")
        else:
            if not 0 < v_dut <= V_MAX_V:
                errs.append(f"DUT voltage must be 0 to {V_MAX_V:g} V")
            if v_dut * i_a > P_MAX_W:
                errs.append(f"{v_dut * i_a:.1f} W exceeds the {P_MAX_W:g} W limit")
        if not 0 <= i_a <= I_MAX_A:
            errs.append(f"current must be 0 to {I_MAX_A:.2f} A")
        if self.locked_out:
            errs.append("thermal lockout: wait for the budget to fall below 60 %")
        return errs

    def turn_on(self, i_a: float, v_dut: float):
        errs = self.check(i_a, v_dut)
        if errs:
            raise ValueError("; ".join(errs))
        code = code_for(i_a)
        tier = tier_for(code)
        ocp = int(min(4095, (i_a + OCP_MARGIN_A) * ISEN_COUNTS_PER_A))
        self.q("DAC 0")
        self.q("CLEAR")
        safe = self.q("*IDN?").startswith("ELOAD-REVA-SAFE")
        # SAFE firmware: VDUT + ISET (it sets tier, OCP and code itself and enforces the ceilings).
        # TEST firmware: the explicit TIER / OCP / DAC sequence.
        cmds = ([f"VDUT {int(round(v_dut * 1000))}", "ARM", f"ISET {int(round(i_a * 1000))}"] if safe
                else [f"TIER {tier}", f"OCP {ocp}", "ARM", f"DAC {code}"])
        for cmd in cmds:
            r = self.q(cmd)
            if not r.startswith("OK"):
                self.off()
                raise RuntimeError(f"{cmd}: {r}")
        self.on, self.i_set, self.v_dut = True, i_a, v_dut

    def set_current(self, i_a: float, v_dut: float):
        if not self.on:
            return
        self.turn_on(i_a, v_dut)

    def off(self):
        self.on = False
        try:
            self.q("DAC 0"); self.q("DISARM")
        except Exception:
            pass

    def poll(self) -> dict:
        """One heartbeat: MEAS?, power, thermal model, limit checks. Returns a status dict."""
        r = self.q("MEAS?").split(",")
        isen, dac, st = float(r[0]), int(r[6]), r[7]
        armed, flags = st[0] == "A", int(st[1:], 16)
        i_meas = max(0.0, (isen - ISEN_ZERO) / ISEN_COUNTS_PER_A)
        v = self.v_dut if self.v_dut else 0.0
        p = v * i_meas if armed else 0.0
        now = time.time()
        dt, self.t_last = min(now - self.t_last, 2.0), now
        self.x1 += dt / TAU1 * (p - self.x1)
        self.x2 += dt / TAU2 * (p - self.x2)
        stress = R1 * self.x1 + R2 * self.x2 + R_DIE * p
        frac = stress / self.limit_K
        msg = ""
        if armed and p > P_MAX_W * 1.05:
            self.off(); msg = f"OFF: measured power {p:.1f} W > {P_MAX_W:g} W"
        elif armed and frac >= 1.0:
            self.off(); self.locked_out = True; msg = "OFF: thermal budget used up (30 W x 3 min equivalent)"
        if self.locked_out and frac < REARM_FRACTION:
            self.locked_out = False
        if self.on and not armed:
            self.on = False
            msg = msg or (f"OFF: firmware fault flags 0x{flags:02X}" if flags else "OFF: firmware disarmed")
        # time to cutoff at the present power (simple forward simulation)
        t_left = None
        if armed and p > 0:
            x1, x2, t = self.x1, self.x2, 0.0
            while t < 3600:
                x1 += 1.0 / TAU1 * (p - x1); x2 += 1.0 / TAU2 * (p - x2); t += 1.0
                if R1 * x1 + R2 * x2 + R_DIE * p >= self.limit_K:
                    t_left = t
                    break
        if len(r) >= 11:                       # SAFE firmware: use its own thermal budget if it is higher
            frac = max(frac, float(r[10]) / 100.0)
        flag_txt = ",".join(n for b, n in ((1, "WDT"), (2, "OCP"), (4, "OTP"), (8, "I2C"), (16, "PWR"), (32, "THERMAL"))
                            if flags & b) or "none"
        self.last = {"i": i_meas, "p": p, "armed": armed, "flags": flag_txt, "dac": dac, "frac": frac,
                     "t_left": t_left, "msg": msg, "locked": self.locked_out}
        return self.last


# --------------------------------------------------------------------------------------------------- UI
def run_ui():
    import tkinter as tk
    from tkinter import ttk, messagebox

    load = Load()
    root = tk.Tk()
    root.title("Rev A DC load")
    root.resizable(False, False)
    pad = {"padx": 6, "pady": 4}
    events: queue.Queue = queue.Queue()

    frm = ttk.Frame(root, padding=10)
    frm.grid()
    # connection
    ttk.Label(frm, text="Port").grid(row=0, column=0, sticky="w", **pad)
    port_var = tk.StringVar()
    port_box = ttk.Combobox(frm, textvariable=port_var, width=28, state="readonly")
    port_box.grid(row=0, column=1, columnspan=2, sticky="we", **pad)

    def refresh_ports():
        ps = load.ports()
        port_box["values"] = [lbl for _, lbl in ps]
        if ps and not port_var.get():
            port_var.set(ps[0][1])
    refresh_ports()
    conn_btn = ttk.Button(frm, text="Connect")
    conn_btn.grid(row=0, column=3, **pad)
    idn_var = tk.StringVar(value="not connected")
    ttk.Label(frm, textvariable=idn_var, foreground="#52514e").grid(row=1, column=0, columnspan=4, sticky="w", **pad)

    # inputs
    ttk.Label(frm, text="DUT voltage (V)").grid(row=2, column=0, sticky="w", **pad)
    v_var = tk.StringVar()
    ttk.Entry(frm, textvariable=v_var, width=10).grid(row=2, column=1, sticky="w", **pad)
    ttk.Label(frm, text=f"max {V_MAX_V:g} V, typed in: VSEN is broken on Rev A",
              foreground="#52514e").grid(row=2, column=2, columnspan=2, sticky="w", **pad)
    ttk.Label(frm, text="Current (A)").grid(row=3, column=0, sticky="w", **pad)
    i_var = tk.DoubleVar(value=0.0)
    i_entry_var = tk.StringVar(value="0.000")
    scale = ttk.Scale(frm, from_=0.0, to=I_MAX_A, variable=i_var, length=260)
    scale.grid(row=3, column=1, columnspan=2, sticky="we", **pad)
    ttk.Entry(frm, textvariable=i_entry_var, width=8).grid(row=3, column=3, **pad)

    # buttons
    on_btn = ttk.Button(frm, text="LOAD ON")
    off_btn = ttk.Button(frm, text="OFF")
    on_btn.grid(row=4, column=1, sticky="we", **pad)
    off_btn.grid(row=4, column=2, sticky="we", **pad)

    # readouts
    big = ("Segoe UI", 20) if root.tk.call("tk", "windowingsystem") == "win32" else ("Helvetica", 22)
    i_meas_var, p_var, state_var, msg_var = (tk.StringVar(value="-") for _ in range(4))
    ttk.Label(frm, text="Measured").grid(row=5, column=0, sticky="w", **pad)
    ttk.Label(frm, textvariable=i_meas_var, font=big).grid(row=5, column=1, sticky="w", **pad)
    ttk.Label(frm, textvariable=p_var, font=big).grid(row=5, column=2, sticky="w", **pad)
    ttk.Label(frm, text="Thermal budget").grid(row=6, column=0, sticky="w", **pad)
    bar = ttk.Progressbar(frm, length=260, maximum=100)
    bar.grid(row=6, column=1, columnspan=2, sticky="we", **pad)
    tl_var = tk.StringVar(value="")
    ttk.Label(frm, textvariable=tl_var).grid(row=6, column=3, sticky="w", **pad)
    ttk.Label(frm, textvariable=state_var).grid(row=7, column=0, columnspan=4, sticky="w", **pad)
    ttk.Label(frm, textvariable=msg_var, foreground="#d03b3b", wraplength=460).grid(row=8, column=0, columnspan=4, sticky="w", **pad)
    ttk.Label(frm, text=f"Limits: {I_MAX_A:.2f} A, {V_MAX_V:g} V, {P_MAX_W:g} W, thermal = 30 W for 3 min. "
                        "Closing this window switches the load off.", foreground="#52514e",
              wraplength=460).grid(row=9, column=0, columnspan=4, sticky="w", **pad)

    def read_inputs():
        try:
            v = float(v_var.get()) if v_var.get().strip() else None
        except ValueError:
            v = None
        try:
            i = float(i_entry_var.get())
        except ValueError:
            i = i_var.get()
        return round(max(0.0, min(I_MAX_A, i)), 3), v

    def on_scale(_=None):
        i_entry_var.set(f"{i_var.get():.3f}")
    scale.configure(command=on_scale)

    def apply_entry(_=None):
        i, v = read_inputs()
        i_var.set(i); i_entry_var.set(f"{i:.3f}")
        if load.on:
            try:
                load.set_current(i, v)
            except Exception as e:
                msg_var.set(str(e))

    def do_connect():
        if load.ser:
            load.close(); idn_var.set("not connected"); conn_btn.configure(text="Connect"); return
        sel = port_var.get().split(" ")[0]
        try:
            idn_var.set(load.connect(sel)); conn_btn.configure(text="Disconnect")
        except Exception as e:
            messagebox.showerror("Connect", str(e)); refresh_ports()

    def do_on():
        i, v = read_inputs()
        if v is not None and v < VDROP_OHM * i + VDROP_OFF + 0.05:
            msg_var.set(f"Note: at {i:.2f} A the load needs about {VDROP_OHM * i + VDROP_OFF:.2f} V or more; "
                        "the current will be lower.")
        try:
            load.turn_on(i, v); msg_var.set("")
        except Exception as e:
            messagebox.showwarning("Load ON", str(e))

    conn_btn.configure(command=do_connect)
    on_btn.configure(command=do_on)
    off_btn.configure(command=lambda: (load.off(), msg_var.set("")))
    scale.bind("<ButtonRelease-1>", apply_entry)
    root.bind("<Return>", apply_entry)

    def poller():
        while True:
            if load.ser:
                try:
                    events.put(load.poll())
                except Exception as e:
                    events.put({"error": str(e)})
            time.sleep(POLL_S)
    threading.Thread(target=poller, daemon=True).start()

    def ui_tick():
        try:
            while True:
                s = events.get_nowait()
                if "error" in s:
                    state_var.set(f"link error: {s['error']}  (the firmware switches off after 2 s without polls)")
                    continue
                i_meas_var.set(f"{s['i']:.3f} A")
                p_var.set(f"{s['p']:.1f} W")
                bar["value"] = min(100.0, s["frac"] * 100)
                tl_var.set("" if s["t_left"] is None else f"cutoff in {int(s['t_left'] // 60)}:{int(s['t_left'] % 60):02d}")
                state_var.set(f"{'ON' if s['armed'] else 'OFF'}   DAC {s['dac']}   faults: {s['flags']}"
                              + ("   THERMAL LOCKOUT" if s["locked"] else ""))
                if s["msg"]:
                    msg_var.set(s["msg"])
        except queue.Empty:
            pass
        root.after(150, ui_tick)
    ui_tick()

    def on_close():
        load.close()
        root.destroy()
    root.protocol("WM_DELETE_WINDOW", on_close)
    root.mainloop()


if __name__ == "__main__":
    run_ui()
