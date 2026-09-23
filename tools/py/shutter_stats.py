#!/usr/bin/env python3
"""Summarize a ThermalView stats CSV (debug "Stats CSV"): frame timing, frozen-frame runs
(shutter cycles), and which metadata words change during them (docs/PLAN.md M1 shutter-cycle
characterization).

    tools/py/.venv/bin/python tools/py/shutter_stats.py STATS.csv [--offset S]

--offset shifts the printed times, e.g. by the seconds between plug-in and stream start, to read
them as time since power-up (docs/DEVICE.md). Frozen runs are counted in every state.
"""

import argparse
import csv
import statistics

FPA_OFF, FPA_DIV = 8617.0, 37.682  # width-256 constants (docs/PROTOCOL.md)


def fpa_c(row):
    return 20.0 - (int(row["P1"]) - FPA_OFF) / FPA_DIV


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("csv")
    ap.add_argument("--offset", type=float, default=0.0, help="seconds added to printed times")
    args = ap.parse_args()
    with open(args.csv, newline="") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise SystemExit("empty CSV")
    t = [float(r["t_ms"]) for r in rows]
    running = [r for r in rows if r["state"] == "Running" and r["bytes"] == "100352"]
    print(f"{len(rows)} rows over {(t[-1] - t[0]) / 1000:.1f} s; {len(running)} full frames while Running")

    # Timing while running.
    rt = [float(r["t_ms"]) for r in running]
    iv = [b - a for a, b in zip(rt, rt[1:])]
    if iv:
        print(f"interval: mean {statistics.fmean(iv):.3f} ms ({1000 / statistics.fmean(iv):.3f} fps), "
              f"sd {statistics.pstdev(iv):.3f} ms, max {max(iv):.1f} ms, gaps >60 ms: {sum(i > 60 for i in iv)}")
    # Every full frame gets a row, whatever the state, so gaps here are frames processing never
    # saw (partial frames are rejected before the CSV, so they show up here too).
    seq = [int(r["seq"]) for r in rows]
    print(f"sequence gaps (all states): {sum(max(0, b - a - 1) for a, b in zip(seq, seq[1:]))}")

    # Frozen runs (identical consecutive frames) anywhere in the log.
    runs, start = [], None
    for i, r in enumerate(rows):
        if r["frozen"] == "1" and start is None:
            start = i
        elif r["frozen"] != "1" and start is not None:
            runs.append((start - 1, i))  # include the frame that was repeated
            start = None
    print(f"\nfrozen runs: {len(runs)} (FPA change since the previous run's start)")
    words = [k for k in rows[0] if k[0] in "PQ" and k[1:].isdigit()]
    prev_fpa = None
    for a, b in runs:
        dur = float(rows[b]["t_ms"]) - float(rows[a]["t_ms"])
        before = rows[max(0, a - 1)]
        during = rows[a + 1: b]
        changed = [w for w in words if len({r[w] for r in during} | {before[w]}) > 1]
        f = fpa_c(rows[a])
        d = "" if prev_fpa is None else f" ({f - prev_fpa:+.2f})"
        prev_fpa = f
        print(f"  t={float(rows[a]['t_ms']) / 1000 + args.offset:8.2f} s  {b - a - 1:3d} repeated frames, "
              f"{dur:6.0f} ms, state {rows[a]['state']}, FPA {f:.2f} C{d}; "
              f"words changing: {' '.join(changed) or 'none'}")
    if runs and runs[-1][1] < len(rows):
        end = runs[-1][1]
        print(f"  after the last run: FPA {fpa_c(rows[end]):.2f} -> {fpa_c(rows[-1]):.2f} C over "
              f"{(float(rows[-1]['t_ms']) - float(rows[end]['t_ms'])) / 1000:.0f} s")

    # Words that behave like a frame counter (step +1 per frame) or stay constant.
    print("\nmetadata words (Running frames):")
    for w in words:
        vals = [int(r[w]) for r in running]
        if not vals:
            continue
        steps = {b - a for a, b in zip(vals, vals[1:])}
        kind = "constant" if len(set(vals)) == 1 else ("counter (+1/frame)" if steps == {1} else "varies")
        print(f"  {w:>4}: {kind:18s} first {vals[0]:6d} last {vals[-1]:6d} min {min(vals):6d} max {max(vals):6d}")

    # Temperatures over time.
    if running:
        fpa = [fpa_c(r) for r in running]
        sh = [int(r["Q1"]) / 10 - 273.15 for r in running]
        print(f"\nFPA: {fpa[0]:.2f} -> {fpa[-1]:.2f} C (min {min(fpa):.2f}, max {max(fpa):.2f}); "
              f"shutter: {sh[0]:.2f} -> {sh[-1]:.2f} C")
        sd = [float(r["img_sd"]) for r in running]
        print(f"image spatial sd: first {sd[0]:.2f}, last {sd[-1]:.2f}, min {min(sd):.2f}, max {max(sd):.2f}")


if __name__ == "__main__":
    main()
