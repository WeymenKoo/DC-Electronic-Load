# Bench test: automation, data and full report

The results are summarised in the [top README](../README.md); the full write-up with every number, method and erratum
is [`REPORT.md`](REPORT.md), and the chronological log is [`results/session_notes.md`](results/session_notes.md).

| Path | What |
|---|---|
| `bench/instruments/` | drivers: KORAD KA3005P (serial), RIGOL DHO804 (USBTMC / VISA), the load (serial) |
| `bench/safety.py` | hard limits, `abort()` (DAC 0 → DISARM → PSU OFF), 3 Hz monitor thread that is also the load's heartbeat |
| `bench/probes.py` | named scope probe maps; every run logs the map it used |
| `bench/tests/` | one script per test (`p0_probecheck`, `p1_rails`, `p2_dac`, `p3_1_current` … `p3_11_shortfall`) |
| `bench/report.py`, `report_p33_p34.py`, `thermal_fit.py`, `step_metrics.py` | plots, fits and metrics from the run folders |
| `results/<date>_<time>_<test>/` | `data.csv`, `meta.json` (instruments, probe map, settings), scope PNGs, `waveforms/*.csv`, plots |
| `results/_combined/` | plots across runs (current tiers, step response) |
| `scope_annotated/` | every raw scope screenshot with a title, conditions, channel legend (signal, probe point, scale, offset) and call-outs; `python tools/annotate_scope.py` regenerates them from `tools/scope_labels.py` |
| `photos/` | bench photos and the GUI screenshot |
| `config.toml` | ports (auto-detected), board calibration, scope probe gain factors |

Run (Python 3.11+, `pip install -r requirements.txt`, NI-VISA for the scope; the load needs the TEST firmware):

```
python -m bench.tests.p0_probecheck --dmm 4.093    # each script prints its plan and asks for 'go'
python -m bench.tests.p3_1_current --tier 2
python -m bench.report                              # plots for every run
```
