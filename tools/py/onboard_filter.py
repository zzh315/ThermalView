#!/usr/bin/env python3
"""What the camera already filters before we see a frame (docs/DEVICE.md "Onboard filtering").

On a static dump (default bench/flat): the per-pixel noise's correlation with later frames (a
recursive temporal filter shows as rho^k), with its neighbours (spatial filtering), and the part all
pixels share. On a dump with motion (default bench/motion): the median response of pixels to a big
step (a moving hand's edge leaving or arriving), which tells a plain recursive filter from a
motion-adaptive one. Counts throughout; each pixel's quadratic trend over the dump is removed first.

    tools/py/.venv/bin/python tools/py/onboard_filter.py [--static DUMP.raw] [--motion DUMP.raw]
"""

import argparse
import pathlib

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
WIDTH, HEIGHT, ROWS = 256, 196, 192


def frames(path):
    return np.fromfile(path, dtype="<u2").reshape(-1, HEIGHT, WIDTH)[:, :ROWS].astype(np.float64)


def detrend(x):
    n = x.shape[0]
    t = np.linspace(-1, 1, n)
    q, _ = np.linalg.qr(np.stack([np.ones(n), t, t * t], axis=1))
    f = x.reshape(n, -1)
    return (f - q @ (q.T @ f)).reshape(x.shape)


def corr(a, b):
    return float((a * b).sum() / np.sqrt((a * a).sum() * (b * b).sum()))


def static(path):
    r = detrend(frames(path))
    print(f"static: {path}")
    print("  temporal correlation of per-pixel noise: " +
          ", ".join(f"lag {k} {corr(r[:-k], r[k:]):+.3f}" for k in (1, 2, 3, 5, 10)))
    print(f"  neighbour correlation: horizontal {corr(r[:, :, :-1], r[:, :, 1:]):+.3f}, "
          f"vertical {corr(r[:, :-1], r[:, 1:]):+.3f}")
    g = r.mean(axis=(1, 2))
    print(f"  per-pixel std (median) {np.median(r.std(axis=0)):.3f} counts; frame-mean std {g.std():.3f} counts, "
          f"lag 1/5 {corr(g[:-1], g[1:]):+.2f}/{corr(g[:-5], g[5:]):+.2f}, largest step {np.abs(np.diff(g)).max():.3f}")


def step_response(x, sign, min_step=150.0, jump=60.0, before=3, after=12):
    """Median normalised response of pixels whose largest one-frame change (of the given sign) exceeds
    `jump`, aligned on that frame; levels from the `before` frames before and the last 3 after."""
    d = np.diff(x, axis=0) * sign
    k = np.argmax(d, axis=0)
    big = np.take_along_axis(d, k[None], 0)[0] > jump
    out = []
    for y, xx in zip(*np.nonzero(big)):
        t = k[y, xx]
        if t < before + 1 or t + after >= x.shape[0]:
            continue
        s = x[t - before:t + after, y, xx]
        start, end = s[:before].mean(), s[-3:].mean()
        if (end - start) * sign < min_step:
            continue
        out.append((s - start) / (end - start))
    return len(out), np.median(np.array(out), axis=0) if out else None


def motion(path):
    x = frames(path)
    print(f"motion: {path}")
    for sign, name in ((-1, "falling (hand leaves)"), (+1, "rising (hand arrives)")):
        n, m = step_response(x, sign)
        if m is None:
            print(f"  {name}: no pixels")
            continue
        print(f"  {name}, {n} pixels, fraction of the step done, from 3 frames before the biggest change:")
        print("    " + " ".join(f"{v:.2f}" for v in m))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--static", default=ROOT / "bench/flat/thermalview.raw", type=pathlib.Path)
    ap.add_argument("--motion", default=ROOT / "bench/motion/thermalview.raw", type=pathlib.Path)
    args = ap.parse_args()
    static(args.static)
    motion(args.motion)


if __name__ == "__main__":
    main()
