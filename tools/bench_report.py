#!/usr/bin/env python3
"""Summarise `rr-game --bench` JSON reports (docs/PERFORMANCE.md).

    tools/bench_report.py REPORT.json [...]            table of runs
    tools/bench_report.py --compare BASE.json NEW.json  per-phase delta
    tools/bench_report.py --min-fps 60 REPORT.json      exit 1 below target
    tools/bench_report.py --max-drift 1 REPORT.json     exit 1 if the 60 Hz clock
                                                        drifted a tick (plan 33b Task 4)
"""
import argparse
import json
import sys


def load(path):
    with open(path) as f:
        return json.load(f)


def table(reports, paths):
    phases = list(reports[0]["phases_ms_mean"].keys())
    head = f"{'run':<12}{'build':<8}{'surface':<11}{'fps':>8}{'mean':>8}{'p95':>8}{'p99':>8}"
    print(head + "".join(f"{p[:9]:>10}" for p in phases))
    for r, p in zip(reports, paths):
        s = f"{r['surface'][0]}x{r['surface'][1]}"
        line = (f"{(r.get('label') or p)[:11]:<12}{r['build']:<8}{s:<11}{r['fps_mean']:>8.1f}"
                f"{r['frame_ms']['mean']:>8.2f}{r['frame_ms']['p95']:>8.2f}{r['frame_ms']['p99']:>8.2f}")
        print(line + "".join(f"{r['phases_ms_mean'][ph]:>10.2f}" for ph in phases))


def compare(base, new):
    print(f"{'metric':<16}{'base ms':>10}{'new ms':>10}{'speedup':>9}")
    rows = [("frame mean", base["frame_ms"]["mean"], new["frame_ms"]["mean"]),
            ("frame p95", base["frame_ms"]["p95"], new["frame_ms"]["p95"])]
    rows += [(k, base["phases_ms_mean"][k], new["phases_ms_mean"].get(k, 0.0)) for k in base["phases_ms_mean"]]
    for name, b, n in rows:
        sp = f"{b / n:.2f}x" if n > 0.0005 else "-"
        print(f"{name:<16}{b:>10.3f}{n:>10.3f}{sp:>9}")
    print(f"fps {base['fps_mean']:.1f} -> {new['fps_mean']:.1f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("reports", nargs="+")
    ap.add_argument("--compare", action="store_true")
    ap.add_argument("--min-fps", type=float)
    ap.add_argument("--max-drift", type=float, help="ticks the race clock may differ from wall time x 60")
    a = ap.parse_args()
    reports = [load(p) for p in a.reports]
    if a.compare:
        if len(reports) != 2:
            ap.error("--compare takes BASE NEW")
        compare(*reports)
    else:
        table(reports, a.reports)
    if a.min_fps is not None:
        low = [r.get("label") or "?" for r in reports if r["fps_mean"] < a.min_fps]
        if low:
            print(f"below {a.min_fps} fps: {', '.join(low)}", file=sys.stderr)
            return 1
    if a.max_drift is not None:
        bad = 0
        for r, path in zip(reports, a.reports):
            pacing = r.get("pacing")
            if not pacing:
                print(f"{path}: no pacing block (older rr-game)", file=sys.stderr)
                bad += 1
                continue
            iv = pacing["interval_ms"]
            print(f"{r.get('label') or path}: {pacing['wall_s']:.1f} s, drift {pacing['drift_ticks']:+.3f} ticks, "
                  f"interval p50 {iv['p50']:.2f} p99 {iv['p99']:.2f} max {iv['max']:.2f} ms, "
                  f"steps 0/1/2/3+ {pacing['step_frames']}, clock resets {pacing.get('clock_resets', 0)}")
            if abs(pacing["drift_ticks"]) >= a.max_drift:
                print(f"{path}: drift {pacing['drift_ticks']:+.3f} ticks exceeds {a.max_drift}", file=sys.stderr)
                bad += 1
        if bad:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
