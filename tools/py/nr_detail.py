#!/usr/bin/env python3
"""Stage 4b's detail loss (owner, 2026-09-25: "it reduces the noise, but the objects lose some
details, even at 1.1"): the study's key-texture metric kept 98%, so it doesn't see what the owner
sees. This measures what a filter takes from the scene's *subtle* structure: surfaces' shading and
low-contrast texture, as strong as the noise or a few times it, the part a weak patch match
averages away.

The reference is the scene itself, nearly noise-free: the time-mean of stages 1-3's signal over one
half of a still scene's frames (75 frames; the camera's own filter correlates them, so the noise
left is ~sigma/4). Each filter runs on every frame of the other half, and the time-mean of its
output is compared with the reference in three bands (differences of Gaussians: fine 0.7-1.5 px,
mid 1.5-3 px, coarse 3-6 px) over the pixels with no strong edge nearby (the reference's 7x7 range
under 12 counts). "kept" is the regression slope of the output's band on the reference's, divided by
the same slope for the unfiltered second half (so the halves' own difference cancels): 1.00 = all of
the band's subtle structure is still there. Also: the per-pixel temporal noise on `flat` and `room`
(the study's), and the keyboard screen edge's 10-90% rise.

That band measure turned out not to separate detail from noise in the fine band: a still scene's
time-mean there is mostly the sensor's fixed pattern, which a filter within a frame can't tell from
noise (every variant keeps it in step with the noise it keeps). So --synthetic measures the filters on
texture whose truth is known: random texture (white noise blurred by 0.7, 1 or 2 px) at 0.5, 1 and 2x
the noise amplitude, added to `flat`'s real frames (real noise and fixed pattern). "kept" is the
regression slope, on the texture, of the time-mean of filter(flat + texture) - filter(flat): the
fraction of the texture's contrast that survives.

Input: stages 1-3's signal, as for nr_study.py (build/nr/in/<scene>/pipeline_s.f32).

    tools/py/.venv/bin/python tools/py/nr_detail.py [--variants NAME,...] [--synthetic]
"""

import argparse
import ctypes
import json
import pathlib
import time

import cv2
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
IN = ROOT / "build" / "nr" / "in"
H, W = 192, 256
SIGMA = 1.07  # the camera's temporal noise per pixel on the still scenes (M4 study), counts


def load(scene, frames):
    return np.fromfile(IN / scene / "pipeline_s.f32", dtype="<f4").reshape(-1, H, W)[frames].astype(np.float32)


def shifted(x, dx, dy, p=8):
    xp = np.pad(x, p, mode="reflect")
    return xp[p + dy:p + dy + H, p + dx:p + dx + W]


def box(x, r):
    return cv2.blur(x, (2 * r + 1, 2 * r + 1), borderType=cv2.BORDER_REFLECT_101)


def nlm(x, search=5, patch=2, strength=1.1, spatial=0.0, cutoff=0.0, norm="l1", addback=0.0, addback_sigma=2.0):
    """The shipped stage 4b (norm l1: mean |patch difference|, weight exp(-(d/h)^2), the pixel itself at
    weight 1), and variants: spatial > 0 weights each offset by a Gaussian of its length (px);
    cutoff > 0 drops candidates whose distance exceeds cutoff x sigma; norm "l2c" is Buades' squared
    distance less the noise's 2 sigma^2; addback > 0 returns that fraction of the removed signal's
    smooth part (a Gaussian of addback_sigma px), where the lost shading is and the removed grain isn't."""
    h = strength * SIGMA
    num = np.zeros_like(x)
    den = np.zeros_like(x)
    for dy in range(-search, search + 1):
        for dx in range(-search, search + 1):
            xd = shifted(x, dx, dy)
            if norm == "l1":
                d = box(np.abs(x - xd), patch)
                w = np.exp(-(d / h) ** 2)
            else:
                d2 = box((x - xd) ** 2, patch)
                w = np.exp(-np.maximum(d2 - 2 * SIGMA ** 2, 0.0) / (h * h))
                d = np.sqrt(d2)
            if cutoff > 0:
                w = np.where(d > cutoff * SIGMA, 0.0, w)
            if spatial > 0:
                w = w * np.exp(-(dx * dx + dy * dy) / (2 * spatial * spatial))
            num += w * xd
            den += w
    out = num / den
    if addback > 0:
        out = out + addback * cv2.GaussianBlur(x - out, (0, 0), addback_sigma, borderType=cv2.BORDER_REFLECT_101)
    return out.astype(np.float32)


_LIB = []


def native():
    """native/core's filters through build/harness/libtvnr.dylib (tools/harness/src/nrlib.cpp)."""
    if not _LIB:
        lib = ctypes.CDLL(str(ROOT / "build" / "harness" / "libtvnr.dylib"))
        f, i, ptr = ctypes.c_float, ctypes.c_int, ctypes.c_void_p
        lib.tv_bm3d.argtypes = [ptr, ptr, f, i, i, i, i, i, f, f, f, i, f, i, f]
        lib.tv_nlm.argtypes = [ptr, ptr, i, i, f]
        _LIB.append(lib)
    return _LIB[0]


def bm3d(x, strength=1.0, block=8, stride=3, search=19, group1=16, group2=32, lam=3.0, tau1=4.0, tau2=0.64,
         wiener=True, kaiser=2.0, all_blocks=True, mu2=0.4):
    """native/core's BM3D (tv/bm3d.h) at sigma = strength x the camera's noise."""
    x = np.ascontiguousarray(x, np.float32)
    out = np.empty_like(x)
    native().tv_bm3d(x.ctypes.data, out.ctypes.data, strength * SIGMA, block, stride, search, group1, group2, lam,
                     tau1, tau2, int(wiener), kaiser, int(all_blocks), mu2)
    return out


def bands(img):
    g = [cv2.GaussianBlur(img.astype(np.float64), (0, 0), s, borderType=cv2.BORDER_REFLECT_101) for s in (0.7, 1.5, 3.0, 6.0)]
    return {"fine": g[0] - g[1], "mid": g[1] - g[2], "coarse": g[2] - g[3]}


def subtle_mask(ref):
    k = np.ones((7, 7), np.uint8)
    rng = cv2.dilate(ref, k) - cv2.erode(ref, k)
    m = rng < 12.0
    m[:8, :] = m[-8:, :] = False
    m[:, :8] = m[:, -8:] = False
    return m


def residual(x):
    t = np.linspace(-1, 1, len(x))
    A = np.stack([np.ones_like(t), t, t * t], 1).astype(np.float64)
    flat = x.reshape(len(x), -1).astype(np.float64)
    coef, *_ = np.linalg.lstsq(A, flat, rcond=None)
    n = (flat - A @ coef).reshape(x.shape)
    return n - np.median(n.reshape(len(n), -1), axis=1)[:, None, None]


def rise_10_90(mean, y0, y1, x0, x1):
    p = mean[y0:y1, x0:x1].mean(axis=0)
    lo, hi = np.percentile(p, 5), np.percentile(p, 95)
    q = (p - lo) / max(hi - lo, 1e-6)
    if q[0] > q[-1]:
        q = q[::-1]
    xs = np.arange(len(q), dtype=float)
    return float(np.interp(0.9, np.maximum.accumulate(q), xs) - np.interp(0.1, np.maximum.accumulate(q), xs))


VARIANTS = {
    "none": None,
    "11x11 s1.1 (now)": dict(search=5, strength=1.1),
    "11x11 s1.0": dict(search=5, strength=1.0),
    "11x11 s0.9": dict(search=5, strength=0.9),
    "11x11 s0.8": dict(search=5, strength=0.8),
    "11x11 s0.7": dict(search=5, strength=0.7),
    "7x7 s1.2": dict(search=3, strength=1.2),
    "5x5 s1.4 (morning)": dict(search=2, strength=1.4),
    "5x5 s1.1": dict(search=2, strength=1.1),
    "11x11 s1.1 spatial 2": dict(search=5, strength=1.1, spatial=2.0),
    "11x11 s1.3 spatial 2": dict(search=5, strength=1.3, spatial=2.0),
    "11x11 s1.1 cutoff 1.6": dict(search=5, strength=1.1, cutoff=1.6),
    "11x11 s1.4 cutoff 1.5": dict(search=5, strength=1.4, cutoff=1.5),
    "11x11 l2c s0.7": dict(search=5, strength=0.7, norm="l2c"),
    "11x11 l2c s1.0": dict(search=5, strength=1.0, norm="l2c"),
    "11x11 s1.1 addback 0.5": dict(search=5, strength=1.1, addback=0.5),
    "11x11 s1.1 addback 1.0": dict(search=5, strength=1.1, addback=1.0),
}
# BM3D variants: the IPOL parameters (block 8, stride 3, search radius 19, groups 16 / 32), then
# the real-time sweep's.
BM3D_VARIANTS = {
    "BM3D ipol s1.0": dict(strength=1.0, lam=2.7, mu2=1.0),
    "BM3D ipol s0.8": dict(strength=0.8, lam=2.7, mu2=1.0),
    "BM3D full groups s1.0": dict(strength=1.0, lam=2.7, mu2=1.0, tau1=0, tau2=0),
    "BM3D 2020 s1.0": dict(strength=1.0, tau1=0, tau2=0),
    "BM3D 2020 s0.8": dict(strength=0.8, tau1=0, tau2=0),
    "BM3D 2020 thr s1.0": dict(strength=1.0),
}


def filter_of(name):
    if name in BM3D_VARIANTS:
        return lambda x, p=BM3D_VARIANTS[name]: bm3d(x, **p)
    params = VARIANTS[name]
    return (lambda x: x) if params is None else (lambda x, p=params: nlm(x, **p))


def texture_cases(seed=3):
    rng = np.random.default_rng(seed)
    cases = {}
    for scale in (0.7, 1.0, 2.0):
        for rms in (0.5, 1.0, 2.0):
            t = cv2.GaussianBlur(rng.standard_normal((H, W)), (0, 0), scale, borderType=cv2.BORDER_REFLECT_101)
            t -= t.mean()
            cases[(scale, rms)] = (t * rms * SIGMA / t.std()).astype(np.float32)
    return cases


def synthetic(names, extra=None, every=3):
    """The known-texture test for the named filters (VARIANTS, BM3D_VARIANTS), and extra: {name: callable}."""
    flat = load("flat", slice(125, 200))
    frames = flat[::every]
    base_noise = residual(frames).std()
    cases = texture_cases()
    print(f"{'variant':26s} {'noise':>6s}  " + " ".join(f"{f'{s}px x{a}':>9s}" for s, a in cases))
    rows = []
    filters = [(name, filter_of(name)) for name in names] + list((extra or {}).items())
    for name, f in filters:
        filtered = np.stack([f(x) for x in frames])
        noise = residual(filtered).std() / base_noise
        base = filtered.mean(0)
        kept = {}
        for (scale, rms), t in cases.items():
            out = np.stack([f(x + t) for x in frames]).mean(0) - base
            kept[f"{scale}px_x{rms}"] = float((out * t).sum() / (t * t).sum())
        rows.append({"variant": name, "noise": float(noise), **kept})
        print(f"{name:26s} {noise:6.2f}  " + " ".join(f"{v:9.2f}" for v in kept.values()), flush=True)
    (ROOT / "build" / "nr").mkdir(parents=True, exist_ok=True)
    (ROOT / "build" / "nr" / "detail_synthetic.json").write_text(json.dumps(rows, indent=1))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--variants", default=",".join(VARIANTS))
    ap.add_argument("--synthetic", action="store_true", help="known texture added to flat's frames")
    args = ap.parse_args()
    if args.synthetic:
        synthetic(args.variants.split(","))
        return
    first, second = slice(50, 125), slice(125, 200)
    scenes = ("room", "keyboard")
    ref = {s: load(s, first).mean(0) for s in scenes}
    data = {s: load(s, second) for s in scenes}
    flat = load("flat", second)
    masks = {s: subtle_mask(ref[s]) for s in scenes}
    ref_bands = {s: bands(ref[s]) for s in scenes}

    def slopes(scene, mean):
        b = bands(mean)
        m = masks[scene]
        return {k: float((b[k][m] * ref_bands[scene][k][m]).sum() / (ref_bands[scene][k][m] ** 2).sum()) for k in b}

    base = {s: slopes(s, data[s].mean(0)) for s in scenes}
    rows = []
    for name in args.variants.split(","):
        params = VARIANTS[name]
        t0 = time.perf_counter()
        f = (lambda x: x) if params is None else (lambda x: nlm(x, **params))
        out = {s: np.stack([f(fr) for fr in data[s]]) for s in scenes}
        out_flat = np.stack([f(fr) for fr in flat])
        row = {"variant": name, "noise_flat": float(residual(out_flat).std()), "noise_room": float(residual(out["room"]).std())}
        for s in scenes:
            sl = slopes(s, out[s].mean(0))
            for k in sl:
                row[f"{s}_{k}"] = sl[k] / base[s][k]
        row["edge_screen"] = float(np.median([rise_10_90(fr, 8, 72, 200, 228) for fr in out["keyboard"]]))
        row["s"] = time.perf_counter() - t0
        rows.append(row)
        print(json.dumps({k: (round(v, 4) if isinstance(v, float) else v) for k, v in row.items()}), flush=True)
    out_dir = ROOT / "build" / "nr"
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "detail.json").write_text(json.dumps(rows, indent=1))
    print(f"\n{'variant':26s} {'noise flat':>10s} {'room':>6s} | {'room: fine':>10s} {'mid':>5s} {'coarse':>6s} | "
          f"{'keyboard: fine':>14s} {'mid':>5s} {'coarse':>6s} | {'edge':>5s}")
    for r in rows:
        print(f"{r['variant']:26s} {r['noise_flat'] / rows[0]['noise_flat']:10.2f} {r['noise_room'] / rows[0]['noise_room']:6.2f} | "
              f"{r['room_fine']:10.2f} {r['room_mid']:5.2f} {r['room_coarse']:6.2f} | {r['keyboard_fine']:14.2f} "
              f"{r['keyboard_mid']:5.2f} {r['keyboard_coarse']:6.2f} | {r['edge_screen']:5.2f}")


if __name__ == "__main__":
    main()
