#!/usr/bin/env python3
"""Find the thermal image's rectangle in an app screenshot (docs/PLAN.md M0 step 4, M3).

Heuristic: the image is the longest run of rows and columns dominated by image-like
pixels — colorful ones for color palettes (--mode color), textured ones for grayscale
palettes (--mode texture). Prints the rectangle in screenshot pixels. Always check the
result against the screenshot: UI overlays and flat scenes can fool it.

    tools/py/.venv/bin/python tools/py/image_rect.py SCREENSHOT.png [--mode texture]
"""

import argparse

import numpy as np
from PIL import Image


def longest_run(flags):
    best, start = (0, 0), None
    for i, on in enumerate(list(flags) + [False]):
        if on and start is None:
            start = i
        elif not on and start is not None:
            if i - start > best[1] - best[0]:
                best = (start, i)
            start = None
    return best


def box_std(gray, k=5):
    """Local standard deviation over a k×k window, via summed-area tables."""
    pad = k // 2
    g = np.pad(gray, pad, mode="edge").astype(np.float64)
    s1 = np.pad(g.cumsum(0).cumsum(1), ((1, 0), (1, 0)))
    s2 = np.pad((g * g).cumsum(0).cumsum(1), ((1, 0), (1, 0)))
    h, w = gray.shape

    def window(s):
        return s[k:k + h, k:k + w] - s[:h, k:k + w] - s[k:k + h, :w] + s[:h, :w]

    n = k * k
    mean = window(s1) / n
    return np.sqrt(np.maximum(window(s2) / n - mean * mean, 0))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("png")
    ap.add_argument("--mode", choices=["color", "texture"], default="color")
    args = ap.parse_args()

    rgb = np.asarray(Image.open(args.png).convert("RGB")).astype(np.float32) / 255
    if args.mode == "color":
        hi, lo = rgb.max(2), rgb.min(2)
        mask = ((hi - lo) / np.maximum(hi, 1e-6) > 0.35) & (hi > 0.15)
    else:
        mask = box_std(rgb.mean(2)) > 0.004

    # Coarse pass: rows/columns with a meaningful share of image pixels; then refine
    # inside that box, where the image should dominate.
    r0, r1 = longest_run(mask.mean(1) > 0.2 * mask.mean(1).max())
    c0, c1 = longest_run(mask.mean(0) > 0.2 * mask.mean(0).max())
    inner = mask[r0:r1, c0:c1]
    dr0, dr1 = longest_run(inner.mean(1) > 0.5)
    dc0, dc1 = longest_run(inner.mean(0) > 0.5)
    x, y, w, h = c0 + dc0, r0 + dr0, dc1 - dc0, dr1 - dr0

    screen_h, screen_w = mask.shape
    print(f"screenshot {screen_w}x{screen_h}: image at x={x} y={y}, {w}x{h} px, "
          f"aspect {w / max(h, 1):.3f} (4:3 = 1.333), {w / 256:.2f}x{h / 192:.2f} "
          f"screen px per camera px")


if __name__ == "__main__":
    main()
