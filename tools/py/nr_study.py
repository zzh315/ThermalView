#!/usr/bin/env python3
"""Spatial noise reduction study (M4, owner request 2026-09-25: reduce the noise tone mapping shows,
within each frame, so nothing can ghost). Compares denoiser families on this camera's recorded
frames, each over a sweep of strengths, before one is built into the pipeline.

Input: the pipeline's signal in raw counts after stages 1-3 (drift, bad pixels, destripe), from
    build/harness/harness bench --out build/nr/in --pipeline "default,detail=0,tone=0" SCENES
Each method runs per frame on that signal (nothing temporal). Metrics, all in counts:
- noise: per-pixel temporal std of the output (frames 50-199, each pixel's quadratic trend and each
  frame's shared offset removed) on the still scenes `flat`, `flat_aged` and `room`; reduction =
  output / input.
- bias: RMS of (time-mean of the output - time-mean of the input) on `keyboard` (texture) and `room`
  (objects): what the method takes away from the scene itself.
- keys: `keyboard`'s key detail (RMS of the time-mean minus its sigma-2 blur inside the keys ROI,
  bench.py's `detail`), output / input.
- edge: `keyboard`'s screen edge and `hand`'s palm edge, 10-90% rise of each frame's profile across
  the edge ROI, median over frames (px).
The noise level sigma the methods scale by is the camera's temporal noise, measured on `flat` (it
doesn't depend on the scene); a single frame's own estimate reads it 1.5-1.8x too high.

    tools/py/.venv/bin/python tools/py/nr_study.py [--methods gf,lee,bilateral,nlm,starlet,gfeps] [--quick]
Writes build/nr/study.json and prints a table.
"""

import argparse
import json
import pathlib
import time

import cv2
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
IN = ROOT / "build" / "nr" / "in"
H, W = 192, 256
FRAMES = slice(50, 200)


def load(scene):
    return np.fromfile(IN / scene / "pipeline_s.f32", dtype="<f4").reshape(-1, H, W)[FRAMES].astype(np.float32)


def box(x, r):
    return cv2.blur(x, (2 * r + 1, 2 * r + 1), borderType=cv2.BORDER_REFLECT)


def smoothstep(lo, hi, v):
    t = np.clip((v - lo) / max(hi - lo, 1e-6), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def gf(x, r, eps):
    """He et al.'s guided filter, the image as its own guide."""
    m = box(x, r)
    v = np.maximum(box(x * x, r) - m * m, 0.0)
    a = v / (v + eps)
    b = m * (1.0 - a)
    return box(a, r) * x + box(b, r)


# The noise level. A single frame's fine-scale MAD reads 1.5-1.8x the temporal noise on real frames
# (the fixed pattern and texture inflate it), so the study scales every method by the temporal noise
# itself: measured on `flat` (calibrate()), the same on every scene (the camera's noise doesn't depend
# on the scene: 1.04-1.08 counts across levels). The pipeline estimates it from frame differences.
SIGMA = [1.0]
MAD_FACTOR = [1.0]


def sigma_of(x):
    return SIGMA[0]


def sigma_single_frame(x):
    """For comparison: the estimate one frame alone gives (MAD of the finest residual)."""
    lap = x - box(x, 1)
    return MAD_FACTOR[0] * float(np.median(np.abs(lap - np.median(lap))))


# ---- denoisers: each takes one frame (counts) and its sigma, returns one frame -------------------

def m_gf(x, s, alpha=0.3, lo=1.0, hi=2.5, r=2, eps=50.0):
    """Stage 6's split, detail attenuated where only noise: out = base + g * detail, g from alpha
    (flat areas) to 1 (texture), by the detail's 3x3 RMS against sigma."""
    base = gf(x, r, eps)
    det = x - base
    rms = np.sqrt(np.maximum(box(det * det, 1), 0.0))
    g = alpha + (1.0 - alpha) * smoothstep(lo * s, hi * s, rms)
    return base + g * det


def m_gfeps(x, s, r=2, k=2.0):
    """The guided filter as a denoiser: eps = k sigma^2 (the local linear MMSE estimate at k = 1)."""
    return gf(x, r, k * s * s)


def m_lee(x, s, r=2, k=1.0):
    """Lee's local Wiener filter: out = mean + max(0, var - k sigma^2) / var * (x - mean)."""
    m = box(x, r)
    v = np.maximum(box(x * x, r) - m * m, 1e-6)
    g = np.clip((v - k * s * s) / v, 0.0, 1.0)
    return m + g * (x - m)


def m_bilateral(x, s, d=5, ss=1.5, k=2.0):
    return cv2.bilateralFilter(x, d, k * s, ss, borderType=cv2.BORDER_REFLECT)


def m_nlm(x, s, h=1.0, tw=5, sw=11):
    """Non-local means (OpenCV, 16-bit, L1 patch distance): the quality reference, far too slow for
    the CPU pipeline."""
    off = float(x.min()) - 100.0
    u = np.clip(np.rint((x - off) * 4.0), 0, 65535).astype(np.uint16)  # quarter counts
    out = cv2.fastNlMeansDenoising(u, h=[h * s * 4.0], templateWindowSize=tw, searchWindowSize=sw,
                                   normType=cv2.NORM_L1)
    return out.astype(np.float32) / 4.0 + off


def half_window(radius):
    """The offsets of a (2r+1)^2 search window, one of each +-pair (the patch distance is symmetric)."""
    return [(dx, dy) for dy in range(0, radius + 1) for dx in range(-radius, radius + 1) if dy > 0 or dx > 0]


# A sparse search: 12 offset pairs spread over an 11x11 window (rings at 1, ~2-3 and ~4-5 px, in
# several directions), the cost of a full 5x5 search.
SPARSE12 = [(1, 0), (0, 1), (1, 1), (1, -1), (2, 1), (-1, 2), (3, 0), (0, 3), (4, 2), (-2, 4), (5, 0), (0, 5)]
SPARSE16 = SPARSE12 + [(2, -2), (2, 2), (3, -3), (3, 3)]


def shifted(x, dx, dy):
    """x(p + d) at every p, borders reflected."""
    p = 8
    xp = np.pad(x, p, mode="reflect")
    return xp[p + dy:p + dy + H, p + dx:p + dx + W]


def m_nlmx(x, s, h=1.0, tw=5, offsets="w7", centre="max", norm="l2"):
    """Our own non-local means (the reference for a C++ port): patch distance = mean squared
    difference over tw x tw, weight exp(-max(d - 2 sigma^2, 0) / (h sigma)^2) (Buades et al.),
    every offset pair evaluated once for both of its pixels, the centre weighted as its best match."""
    offs = {"w5": half_window(2), "w7": half_window(3), "w11": half_window(5), "s12": SPARSE12, "s16": SPARSE16}[offsets]
    num = np.zeros_like(x)
    den = np.zeros_like(x)
    best = np.zeros_like(x)
    r = tw // 2
    hh = (h * s) ** 2
    for dx, dy in offs:
        xd = shifted(x, dx, dy)
        if norm == "l1":  # OpenCV's 16-bit form: the patches' mean absolute difference, a Gaussian weight
            w = np.exp(-(box(np.abs(x - xd), r) ** 2) / hh)
        else:
            w = np.exp(-np.maximum(box((x - xd) ** 2, r) - 2 * s * s, 0.0) / hh)
        num += w * xd
        den += w
        best = np.maximum(best, w)
        # the same pair seen from the other pixel: p + d gets x(p) with the same weight
        wb = shifted(w, -dx, -dy)
        num += wb * shifted(x, -dx, -dy)  # x(q - d) at q
        den += wb
        best = np.maximum(best, wb)
    # Where no neighbour matches at all (strong texture: every weight underflows), the pixel keeps its value.
    wc = np.maximum(best, 1e-6) if centre == "max" else np.ones_like(x)  # "one": the centre's own distance is 0
    return (num + wc * x) / (den + wc)


STARLET_NOISE = []  # per-scale noise std relative to sigma, measured on `flat` (calibrate())


def starlet(x, levels):
    """The isotropic undecimated wavelet (B3 spline, a trous): scales w_1..w_J and the coarse c_J."""
    k = np.array([1, 4, 6, 4, 1], np.float32) / 16.0
    c = x
    ws = []
    for j in range(levels):
        step = 2 ** j
        kj = np.zeros(4 * step + 1, np.float32)
        kj[::step] = k
        n = cv2.sepFilter2D(c, -1, kj, kj, borderType=cv2.BORDER_REFLECT)
        ws.append(c - n)
        c = n
    return ws, c


def m_starlet(x, s, k=3.0, levels=3, soft=False):
    ws, c = starlet(x, levels)
    out = c.copy()
    for j, w in enumerate(ws):
        t = k * s * STARLET_NOISE[j]
        out += np.sign(w) * np.maximum(np.abs(w) - t, 0.0) if soft else w * (np.abs(w) > t)
    return out


METHODS = {"gf": m_gf, "gfeps": m_gfeps, "lee": m_lee, "bilateral": m_bilateral, "nlm": m_nlm, "nlmsmall": m_nlm,
           "nlmx": m_nlmx, "nlml1": m_nlmx, "starlet": m_starlet}

SWEEPS = {
    "gf": [dict(alpha=a, lo=lo, hi=hi) for a in (0.0, 0.25, 0.5) for lo, hi in ((0.75, 1.5), (1.0, 2.5), (1.5, 3.5))],
    "gfeps": [dict(r=r, k=k) for r in (1, 2, 3) for k in (1.0, 2.0, 4.0, 8.0)],
    "lee": [dict(r=r, k=k) for r in (1, 2, 3) for k in (0.5, 1.0, 1.5, 2.0)],
    "bilateral": [dict(d=d, ss=ss, k=k) for d, ss in ((5, 1.0), (7, 1.5)) for k in (1.0, 2.0, 3.0)],
    "nlm": [dict(h=h, tw=5, sw=11) for h in (0.6, 0.9, 1.2, 1.6)],
    "nlmsmall": [dict(h=h, tw=tw, sw=sw) for tw, sw in ((3, 5), (3, 7), (5, 7), (3, 11)) for h in (0.9, 1.2, 1.5)],
    "nlmx": [dict(h=h, tw=tw, offsets=o) for o, tw in (("w5", 3), ("w5", 5), ("s12", 5), ("w7", 3), ("w7", 5), ("w11", 5))
             for h in (0.8, 1.0, 1.2)],
    "nlml1": [dict(h=h, tw=tw, offsets=o, norm="l1", centre=c) for o, tw in (("w5", 3), ("w5", 5), ("w7", 5), ("w11", 5))
              for c in ("one", "max") for h in (0.9, 1.2, 1.5)],
    "starlet": [dict(k=k, levels=lv, soft=sf) for lv in (2, 3) for k in (2.0, 3.0) for sf in (False, True)],
}


# ---- metrics -------------------------------------------------------------------------------------

def residual(x):
    t = np.linspace(-1, 1, len(x))
    A = np.stack([np.ones_like(t), t, t * t], 1).astype(np.float64)
    flat = x.reshape(len(x), -1).astype(np.float64)
    coef, *_ = np.linalg.lstsq(A, flat, rcond=None)
    n = (flat - A @ coef).reshape(x.shape)
    return n - np.median(n.reshape(len(n), -1), axis=1)[:, None, None]


def temporal_noise(x):
    return float(residual(x).std())


def key_detail(mean):
    roi = json.loads((ROOT / "bench" / "keyboard" / "roi.json").read_text())["detail"][0]
    blur = cv2.GaussianBlur(mean.astype(np.float64), (0, 0), 2.0, borderType=cv2.BORDER_REFLECT)
    s = (slice(roi["y"], roi["y"] + roi["h"]), slice(roi["x"], roi["x"] + roi["w"]))
    return float(np.sqrt(np.mean((mean - blur)[s] ** 2)))


def rise_10_90(mean, y0, y1, x0, x1, axis):
    """Mean edge profile across a ROI (averaged along the edge), its 10-90% rise in px."""
    p = mean[y0:y1, x0:x1].mean(axis=0 if axis == "x" else 1)
    lo, hi = np.percentile(p, 5), np.percentile(p, 95)
    q = (p - lo) / max(hi - lo, 1e-6)
    if q[0] > q[-1]:
        q = q[::-1]
    xs = np.arange(len(q), dtype=float)
    a = np.interp(0.1, np.maximum.accumulate(q), xs)
    b = np.interp(0.9, np.maximum.accumulate(q), xs)
    return float(b - a)


def calibrate(flat):
    """sigma's MAD factor and the starlet's per-scale noise, from `flat`'s pure-noise residual."""
    n = residual(flat).astype(np.float32)
    true = float(n.std())
    raw = [float(np.median(np.abs((f - box(f, 1)) - np.median(f - box(f, 1))))) for f in n[:40]]
    MAD_FACTOR[0] = true / float(np.mean(raw))
    SIGMA[0] = true
    ws = [starlet(f, 4)[0] for f in n[:40]]
    STARLET_NOISE[:] = [float(np.mean([w[j].std() for w in ws])) / true for j in range(4)]
    return true


def run(method, params, data):
    f = METHODS[method]
    t0 = time.perf_counter()
    out = {}
    count = 0
    for scene, x in data.items():
        y = np.empty_like(x)
        for i in range(len(x)):
            y[i] = f(x[i], sigma_of(x[i]), **params)
            count += 1
        out[scene] = y
    ms = 1000 * (time.perf_counter() - t0) / count
    res = {"method": method, "params": params, "ms_python": round(ms, 2)}
    for scene in ("flat", "flat_aged", "room"):
        res[f"noise_{scene}"] = round(temporal_noise(out[scene]), 4)
    for scene in ("keyboard", "room"):
        res[f"bias_{scene}"] = round(float(np.sqrt(np.mean((out[scene].mean(0) - data[scene].mean(0)) ** 2))), 4)
    res["keys"] = round(key_detail(out["keyboard"].mean(0)), 4)
    res["edge_screen"], res["edge_palm"] = edges(out)
    return res


def edges(out):
    """10-90% rises, per frame (the hand moves), medians: keyboard's screen edge, hand's palm edge
    (the ROIs of bench/<scene>/roi.json)."""
    screen = np.median([rise_10_90(f, 8, 72, 200, 228, "x") for f in out["keyboard"]])
    palm = np.median([rise_10_90(f, 120, 170, 184, 204, "y") for f in out["hand"]])
    return round(float(screen), 3), round(float(palm), 3)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--methods", default="gf,gfeps,lee,bilateral,starlet,nlm")
    ap.add_argument("--quick", action="store_true", help="every 3rd frame")
    args = ap.parse_args()
    scenes = ("flat", "flat_aged", "room", "keyboard", "hand")
    data = {s: load(s) for s in scenes}
    if args.quick:
        data = {s: x[::3] for s, x in data.items()}
    sigma = calibrate(data["flat"])
    print(f"flat's per-pixel noise {sigma:.3f} counts; MAD factor {MAD_FACTOR[0]:.3f}; starlet per-scale noise "
          f"{[round(v, 3) for v in STARLET_NOISE]}; a single frame's own estimate on flat/room/keyboard: "
          f"{[round(sigma_single_frame(data[s][0]), 3) for s in ('flat', 'room', 'keyboard')]}")
    ident = {"method": "none", "params": {}}
    ident.update({f"noise_{s}": round(temporal_noise(data[s]), 4) for s in ("flat", "flat_aged", "room")})
    ident.update({"bias_keyboard": 0.0, "bias_room": 0.0, "keys": round(key_detail(data["keyboard"].mean(0)), 4),
                  "ms_python": 0})
    ident["edge_screen"], ident["edge_palm"] = edges(data)
    results = [ident]
    for method in args.methods.split(","):
        for params in SWEEPS[method]:
            r = run(method, params, data)
            results.append(r)
            print(json.dumps(r))
    (ROOT / "build" / "nr").mkdir(parents=True, exist_ok=True)
    (ROOT / "build" / "nr" / "study.json").write_text(json.dumps({"sigma": sigma, "results": results}, indent=1))
    base = results[0]
    print(f"\n{'method':10s} {'params':38s} {'noise flat':>10s} {'aged':>6s} {'room':>6s} {'keys':>6s} "
          f"{'bias kb':>8s} {'bias rm':>8s} {'screen':>7s} {'palm':>6s} {'ms':>6s}")
    for r in results:
        print(f"{r['method']:10s} {json.dumps(r['params'])[:38]:38s} "
              f"{r['noise_flat'] / base['noise_flat']:10.3f} {r['noise_flat_aged'] / base['noise_flat_aged']:6.3f} "
              f"{r['noise_room'] / base['noise_room']:6.3f} {r['keys'] / base['keys']:6.3f} {r['bias_keyboard']:8.3f} "
              f"{r['bias_room']:8.3f} {r['edge_screen']:7.2f} {r['edge_palm']:6.2f} {r['ms_python']:6.1f}")


if __name__ == "__main__":
    main()
