#!/usr/bin/env python3
"""The camera's per-pixel drift map (docs/PLAN.md M4 stage 3; PIPELINE_LOG stage 3).

Between shutter calibrations every pixel's offset drifts at its own fixed rate as the focal plane
warms, and the pattern is the same from one warm-up to the next (M4, 2026-09-25: maps from two
sessions correlate at +0.99). Each frame's metadata says how far it has drifted: the shutter
temperature (Q+1) holds its value from the last calibration, so dT = (FPA - shutter) - c0, with c0
the ~0.4 °C they differ by right after one.

Input: calibration periods, each a set of dumps of one still scene between two calibrations (same
shutter temperature). Per period, each pixel's time-averaged value is fitted against dT; the slope
is its rate in counts per °C (the scene and the period's starting pattern go into the intercept).
Periods are combined, weighted by how much dT each spans; the map keeps zero mean. Also reports how
well each period's map predicts the others (cross-validation) and how linear the fits are.

Writes a float32 little-endian 256x192 map and a JSON sidecar with its provenance. Software only:
nothing is written to the camera (CLAUDE.md rule 2).

    tools/py/.venv/bin/python tools/py/drift_map.py --period A.raw B.raw ... [--period ...] \\
        --out native/core/data/drift_KA1213.f32
A dump may be given as path.raw:first:last to use a frame range.
"""

import argparse
import json
import pathlib

import numpy as np

W, H, ROWS = 256, 196, 192
FPA_OFF, FPA_DIV = 8617.0, 37.682  # width-256 constants (docs/PROTOCOL.md)
C0 = 0.40                           # FPA - shutter right after a calibration (M4: 0.35-0.5)


def read(spec):
    path, *rng = spec.split(":")
    x = np.fromfile(path, dtype="<u2")
    n = x.size // (W * H)
    x = x[:n * W * H].reshape(n, H, W)
    if rng:
        x = x[int(rng[0]):int(rng[1])]
    fpa = 20.0 - (x[:, ROWS, 1].astype(np.float64) - FPA_OFF) / FPA_DIV
    shutter = x[:, ROWS + 1, 1].astype(np.float64) / 10.0 - 273.15
    return x[:, :ROWS].astype(np.float64).mean(axis=0), float((fpa - shutter).mean() - C0), float(shutter.mean())


def fit(dumps):
    means, dts, shutters = zip(*(read(d) for d in dumps))
    if max(shutters) - min(shutters) > 0.05:
        raise SystemExit(f"these dumps span a calibration (shutter {min(shutters):.2f}..{max(shutters):.2f}): {dumps}")
    t = np.array(dts)
    y = np.stack(means).reshape(len(t), -1)
    A = np.stack([np.ones_like(t), t], axis=1)
    coef, *_ = np.linalg.lstsq(A, y, rcond=None)
    slope = coef[1].reshape(ROWS, W)
    resid = y - A @ coef
    return slope - slope.mean(), t, float(resid.std()), float(((t - t.mean()) ** 2).sum())


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--period", nargs="+", action="append", required=True, help="dumps between two calibrations")
    ap.add_argument("--out", type=pathlib.Path, required=True)
    args = ap.parse_args()
    periods = [fit(p) for p in args.period]
    for p, (m, t, res, w) in zip(args.period, periods):
        print(f"period {len(p)} dumps, dT {t.min():.2f}..{t.max():.2f} °C: map std {m.std():.3f} counts/°C, "
              f"fit residual {res:.3f} counts")
    cross = []
    for i, (mi, *_) in enumerate(periods):
        for j, (mj, *_) in enumerate(periods):
            if i < j:
                c = float(np.corrcoef(mi.ravel(), mj.ravel())[0, 1])
                cross.append({"periods": [i, j], "correlation": round(c, 4),
                              "std_ratio": round(float(mi.std() / mj.std()), 3)})
                print(f"periods {i} and {j}: correlation {c:+.4f}, std ratio {mi.std() / mj.std():.3f}")
    weights = np.array([w for *_, w in periods])
    drift = sum(wt * m for wt, (m, *_) in zip(weights, periods)) / weights.sum()
    drift = (drift - drift.mean()).astype("<f4")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    drift.tofile(args.out)
    sidecar = {"format": "float32 little-endian, 192 rows x 256 columns, counts per °C of (FPA - shutter - c0)",
               "c0_c": C0, "std_counts_per_c": round(float(drift.std()), 4),
               "min": round(float(drift.min()), 3), "max": round(float(drift.max()), 3),
               "periods": [{"dumps": p, "dT_c": [round(float(v), 3) for v in t], "fit_residual_counts": round(res, 3)}
                           for p, (m, t, res, w) in zip(args.period, periods)],
               "cross_validation": cross, "tool": "tools/py/drift_map.py"}
    args.out.with_suffix(".json").write_text(json.dumps(sidecar, indent=2) + "\n")
    print(f"wrote {args.out} (std {drift.std():.3f}, range {drift.min():.2f}..{drift.max():.2f} counts/°C)")


if __name__ == "__main__":
    main()
