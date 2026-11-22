"""Bench configuration and instrument auto-detection."""
from __future__ import annotations

import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]          # bench/bench_test
RESULTS = ROOT / "results"

LOAD_VIDPID = (0x1A86, 0x7523)   # CH340E on control board J3
PSU_VIDPID = (0x0416, 0x5011)    # KA3005P virtual COM
SCOPE_VID = "0X1AB1"             # Rigol


def load_config(path: Path | None = None) -> dict:
    with open(path or ROOT / "config.toml", "rb") as f:
        return tomllib.load(f)


def find_serial(vid: int, pid: int) -> str:
    from serial.tools import list_ports
    hits = [p.device for p in list_ports.comports() if p.vid == vid and p.pid == pid]
    if len(hits) != 1:
        raise RuntimeError(f"expected one port with VID:PID {vid:04X}:{pid:04X}, found {hits}")
    return hits[0]


def find_scope(rm) -> str:
    hits = [r for r in rm.list_resources("?*") if SCOPE_VID in r.upper() and r.upper().startswith(("USB", "TCPIP"))]
    if len(hits) != 1:
        raise RuntimeError(f"expected one Rigol VISA resource, found {hits}")
    return hits[0]


def resolve_ports(cfg: dict, rm=None, need=("load", "psu", "scope")) -> dict:
    p = dict(cfg.get("ports", {}))
    if "load" in need and p.get("load", "auto") == "auto":
        p["load"] = find_serial(*LOAD_VIDPID)
    if "psu" in need and p.get("psu", "auto") == "auto":
        p["psu"] = find_serial(*PSU_VIDPID)
    if "scope" in need and p.get("scope", "auto") == "auto" and rm is not None:
        p["scope"] = find_scope(rm)
    return {k: v for k, v in p.items() if k in need}
