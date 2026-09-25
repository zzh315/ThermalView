#!/usr/bin/env python3
"""A palette's colors, cold to hot, recovered from a screenshot of another app's image (observing its
output only: CLAUDE.md's rule for closed apps).

Every pixel of a palette-mapped image lies on the palette's curve through color space, so the curve
can be read back from any image with enough range. This keeps the smooth parts of the image (JPEG's
chroma mixing at sharp edges falls off the curve), takes their colors in OKLab, and orders them by
hue: a rainbow palette's hue runs one way round the wheel, from its cold end to its hot end, so the
widest empty stretch of hue is where the curve's two ends meet. Along the ordered hue, each slice's
median lightness and chroma give the curve; resampled at even steps of OKLab distance, that's the
palette's stops (the app's own spacing along the curve isn't visible in one image; even perceptual
steps are the neutral choice).

    tools/py/.venv/bin/python tools/py/palette_from_screenshot.py SHOT.jpg --rect X,Y,W,H \\
        [--stops 16] [--out palettes/NAME.json --name NAME]

Prints the curve (hue, lightness, chroma) and the stops; --out writes a palette file (OKLCh,
"interpolate": each stop exactly, the hue along the shorter arc between them).
"""

import argparse
import json

import cv2
import numpy as np
from PIL import Image


def srgb_to_oklab(rgb):
    lin = np.where(rgb <= 0.04045, rgb / 12.92, ((rgb + 0.055) / 1.055) ** 2.4)
    l = 0.4122214708 * lin[..., 0] + 0.5363325363 * lin[..., 1] + 0.0514459929 * lin[..., 2]
    m = 0.2119034982 * lin[..., 0] + 0.6806995451 * lin[..., 1] + 0.1073969566 * lin[..., 2]
    s = 0.0883024619 * lin[..., 0] + 0.2817188376 * lin[..., 1] + 0.6299787005 * lin[..., 2]
    l, m, s = np.cbrt(l), np.cbrt(m), np.cbrt(s)
    return np.stack([
        0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
        1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
        0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s,
    ], -1)


def oklab_to_srgb(lab):
    L, a, b = lab[..., 0], lab[..., 1], lab[..., 2]
    l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
    m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
    s = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3
    lin = np.stack([
        4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
    ], -1)
    lin = np.clip(lin, 0, 1)
    return np.where(lin <= 0.0031308, 12.92 * lin, 1.055 * lin ** (1 / 2.4) - 0.055)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("shot")
    ap.add_argument("--rect", required=True, help="the image inside the screenshot: X,Y,W,H")
    ap.add_argument("--stops", type=int, default=16)
    ap.add_argument("--smooth", type=float, default=0.012, help="most local variation (OKLab) a kept pixel's 7x7 may have")
    ap.add_argument("--out")
    ap.add_argument("--name")
    args = ap.parse_args()

    x, y, w, h = (int(v) for v in args.rect.split(","))
    rgb = np.asarray(Image.open(args.shot).convert("RGB")).astype(np.float32)[y:y + h, x:x + w] / 255
    lab = srgb_to_oklab(rgb).astype(np.float32)
    # The smooth parts: low variation over a 7x7 window; their local mean is the sample (less JPEG noise).
    mean = cv2.blur(lab, (7, 7))
    var = cv2.blur(lab * lab, (7, 7)) - mean * mean
    keep = np.sqrt(np.clip(var, 0, None).sum(-1)) < args.smooth
    samples = mean[keep]
    C = np.hypot(samples[:, 1], samples[:, 2])
    samples = samples[C > 0.04]  # (near-grey pixels have no reliable hue)
    hue = np.degrees(np.arctan2(samples[:, 2], samples[:, 1])) % 360
    print(f"{keep.mean() * 100:.0f}% of the image is smooth; {len(samples)} colored samples")

    # The ends meet in the widest empty stretch of hue (1-degree bins with too few samples count as empty).
    counts = np.bincount(hue.astype(int) % 360, minlength=360)
    empty = counts < max(20, len(samples) // 20000)
    best, best_start = 0, 0
    for s in range(360):
        n = 0
        while n < 360 and empty[(s + n) % 360]:
            n += 1
        if n > best:
            best, best_start = n, s
    gap_mid = (best_start + best / 2) % 360
    print(f"widest gap in hue: {best} degrees from {best_start} (the ends meet at {gap_mid:.0f})")
    # Hue unwrapped to run from just past the gap round to just before it. A rainbow runs from violet
    # (cold) down through blue, green and yellow to red (hot): hue falls from cold to hot.
    u = (gap_mid - hue) % 360  # 0 at the gap, rising the way hue falls
    order = np.argsort(u)
    u, samples = u[order], samples[order]

    # The curve: in 1.5-degree slices, the median lightness and chroma (slices with enough samples).
    edges = np.arange(0, 360 + 1.5, 1.5)
    pts = []
    for i in range(len(edges) - 1):
        sel = (u >= edges[i]) & (u < edges[i + 1])
        if sel.sum() < 30:
            continue
        s = samples[sel]
        hm = (gap_mid - (edges[i] + edges[i + 1]) / 2) % 360  # (the slice's own hue)
        pts.append((hm, np.median(s[:, 0]), np.median(np.hypot(s[:, 1], s[:, 2])), sel.sum()))
    pts = np.array(pts)
    # (a light running median over neighbours, so JPEG's noise doesn't wiggle the curve)
    Ls = np.array([np.median(pts[max(0, i - 2):i + 3, 1]) for i in range(len(pts))])
    Cs = np.array([np.median(pts[max(0, i - 2):i + 3, 2]) for i in range(len(pts))])
    hr = np.radians(pts[:, 0])
    curve = np.stack([Ls, Cs * np.cos(hr), Cs * np.sin(hr)], -1)
    print("cold to hot: hue, L, C (every 6th slice)")
    for i in range(0, len(pts), 6):
        print(f"  {pts[i, 0]:6.1f}  {Ls[i]:.3f}  {Cs[i]:.3f}  ({int(pts[i, 3])} samples)")

    # Even steps along the curve's OKLab length.
    seg = np.linalg.norm(np.diff(curve, axis=0), axis=1)
    arc = np.concatenate([[0], np.cumsum(seg)])
    at = np.linspace(0, arc[-1], args.stops)
    stops = np.stack([np.interp(at, arc, curve[:, k]) for k in range(3)], -1)
    hexes = ["#%02X%02X%02X" % tuple(int(round(255 * c)) for c in oklab_to_srgb(p)) for p in stops]
    print("stops:", " ".join(hexes))
    if args.out:
        spec = {
            "name": args.name or "palette",
            "space": "oklch",
            "chroma": "interpolate",
            "stops": [[round(float(t), 4), hx] for t, hx in zip(np.linspace(0, 1, args.stops), hexes)],
        }
        with open(args.out, "w") as f:
            json.dump(spec, f, indent=2)
            f.write("\n")
        print("wrote", args.out)


if __name__ == "__main__":
    main()
