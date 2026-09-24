#!/usr/bin/env python3
"""Bad-pixel candidates from dumps of a uniform surface (docs/PLAN.md M4 stage 2; PRIOR_ART pass 2
item 8). Offline and read-only: nothing is ever written to the camera (CLAUDE.md rule 2).

Per dump, after removing each pixel's quadratic trend over time:
- offset: the time-averaged value against the median of its 5x5 ring (the inner 3x3 left out,
  because the camera couples neighbours, DEVICE.md "Onboard filtering"); flagged beyond 6 robust sigma;
- noisy / stuck: temporal std above 3x or below 0.2x the median (pixels at the clip, raw >= 13700,
  read as stuck and are left out);
- blinker: the largest frame-to-frame jump of the pixel minus its 3x3 median, beyond 8x the
  median of those jumps.
A pixel is a candidate only when flagged in at least two dumps. Writes a JSON report.

    tools/py/.venv/bin/python tools/py/bad_pixels.py DUMP.raw DUMP.raw ... [--out FILE]
"""

import argparse
import json
import pathlib

import cv2
import numpy as np

W, H, ROWS = 256, 196, 192
CLIP_FLOOR = 13700


def frames(path):
    x = np.fromfile(path, dtype="<u2")
    n = x.size // (W * H)
    return x[:n * W * H].reshape(n, H, W)[:, :ROWS]


def detrend(x):
    n = x.shape[0]
    t = np.linspace(-1, 1, n)
    q, _ = np.linalg.qr(np.stack([np.ones(n), t, t * t], axis=1))
    f = x.reshape(n, -1)
    return (f - q @ (q.T @ f)).reshape(x.shape)


def ring_median(img):
    """Median of each pixel's 5x5 neighbourhood without its inner 3x3 (16 values)."""
    p = np.pad(img, 2, mode="reflect")
    offs = [(dy, dx) for dy in range(-2, 3) for dx in range(-2, 3) if max(abs(dy), abs(dx)) == 2]
    stack = np.stack([p[2 + dy:2 + dy + img.shape[0], 2 + dx:2 + dx + img.shape[1]] for dy, dx in offs])
    return np.median(stack, axis=0)


def robust_sigma(v):
    return 1.4826 * np.median(np.abs(v - np.median(v)))


def analyse(path):
    raw = frames(path)
    x = raw.astype(np.float64)
    clipped = (raw >= CLIP_FLOOR).any(axis=0)
    mean = x.mean(axis=0)
    e = mean - ring_median(mean)
    s_e = robust_sigma(e)
    r = detrend(x)
    std = r.std(axis=0)
    med_std = np.median(std)
    med3 = np.stack([cv2.medianBlur(f.astype(np.float32), 3) for f in x])
    jumps = np.abs(np.diff(x - med3, axis=0)).max(axis=0)
    med_jump = np.median(jumps)
    flags = {
        "offset": np.abs(e) > 6 * s_e,
        "noisy": std > 3 * med_std,
        "stuck": (std < 0.2 * med_std) & ~clipped,
        "blinker": jumps > 8 * med_jump,
    }
    summary = {"frames": int(x.shape[0]), "offset_sigma": round(float(s_e), 3),
               "max_offset_sigmas": round(float(np.abs(e).max() / s_e), 2),
               "std_range_x_median": [round(float(std.min() / med_std), 2), round(float(std.max() / med_std), 2)],
               "max_jump_x_median": round(float(jumps.max() / med_jump), 2),
               "flagged": {k: int(v.sum()) for k, v in flags.items()}}
    return flags, summary


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dumps", nargs="+", type=pathlib.Path)
    ap.add_argument("--out", type=pathlib.Path)
    args = ap.parse_args()
    votes = np.zeros((ROWS, W), dtype=int)
    reasons = {}
    report = {"dumps": {}}
    for d in args.dumps:
        flags, summary = analyse(d)
        report["dumps"][str(d)] = summary
        print(d, json.dumps(summary))
        any_flag = np.zeros((ROWS, W), dtype=bool)
        for k, v in flags.items():
            any_flag |= v
            for yy, xx in zip(*np.nonzero(v)):
                reasons.setdefault((int(xx), int(yy)), set()).add(k)
        votes += any_flag
    bad = [(int(xx), int(yy)) for yy, xx in zip(*np.nonzero(votes >= min(2, len(args.dumps))))]
    report["candidates"] = [{"x": xx, "y": yy, "reasons": sorted(reasons[(xx, yy)]), "dumps": int(votes[yy, xx])}
                            for xx, yy in bad]
    print(f"candidates flagged in >= {min(2, len(args.dumps))} dumps: {len(bad)}", bad[:20])
    if args.out:
        args.out.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
