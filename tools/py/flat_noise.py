#!/usr/bin/env python3
"""Noise on dumps of a uniform surface (docs/PLAN.md M1 / M3 `flat`): temporal noise, fixed
pattern after removing a low-order 2D polynomial, and column/row stripes. Run it on dumps taken
at intervals after a shutter cycle to see how the fixed pattern grows.

    tools/py/.venv/bin/python tools/py/flat_noise.py DUMP [DUMP ...]
"""

import argparse
import json
import pathlib

import numpy as np

WIDTH, HEIGHT, ROWS = 256, 196, 192
FPA_OFF, FPA_DIV = 8617.0, 37.682  # width-256 constants (docs/PROTOCOL.md)


def detrend(img, order=2):
    """Residual after a least-squares 2D polynomial of the given order."""
    y, x = np.mgrid[0:img.shape[0], 0:img.shape[1]]
    x = (x - x.mean()) / x.std()
    y = (y - y.mean()) / y.std()
    terms = [x**i * y**j for i in range(order + 1) for j in range(order + 1 - i)]
    a = np.stack([t.ravel() for t in terms], axis=1)
    coef, *_ = np.linalg.lstsq(a, img.ravel(), rcond=None)
    return img - (a @ coef).reshape(img.shape)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dumps", nargs="+")
    args = ap.parse_args()
    print(f"{'dump':28s} {'frames':>6s} {'t0 (s)':>8s} {'FPA C':>7s} {'temporal':>9s} "
          f"{'fixed':>7s} {'columns':>8s} {'rows':>6s} {'mean':>8s}")
    first_ts = None
    for d in args.dumps:
        base = pathlib.Path(d)
        if base.suffix in (".raw", ".json"):
            base = base.with_suffix("")
        frames = np.fromfile(base.with_suffix(".raw"), dtype="<u2").reshape(-1, HEIGHT, WIDTH)
        meta = json.loads(base.with_suffix(".json").read_text())
        ts = meta["timestamps_ns"][0] / 1e9
        first_ts = ts if first_ts is None else first_ts
        img = frames[:, :ROWS, :].astype(np.float64)
        fpa = 20.0 - (frames[:, ROWS, 1].astype(np.float64).mean() - FPA_OFF) / FPA_DIV
        temporal = float(np.median(img.std(axis=0, ddof=1)))  # per-pixel std over time, median
        mean = img.mean(axis=0)
        resid = detrend(mean)
        fixed = float(resid.std())                    # spatial std of the time-averaged residual
        columns = float(resid.mean(axis=0).std())     # column-mean stripes
        rows = float(resid.mean(axis=1).std())        # row-mean stripes
        print(f"{base.name:28s} {len(frames):6d} {ts - first_ts:8.1f} {fpa:7.2f} {temporal:9.3f} "
              f"{fixed:7.3f} {columns:8.3f} {rows:6.3f} {mean.mean():8.1f}")
    print("\nUnits: raw counts. 'fixed' still contains the temporal noise divided by sqrt(frames).")


if __name__ == "__main__":
    main()
