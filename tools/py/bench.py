#!/usr/bin/env python3
"""Benchmark run (docs/PLAN.md M3): harness output -> metrics, contact sheets and clips.

Builds and runs `harness bench` (native/core's display path on every bench/<scene>/ dump), then:

- bench/results/<commit>[-dirty].json: the metrics below. <commit> is the last one that touched
  native/core/, tools/harness/, palettes/ or this file; "-dirty" while they have uncommitted changes.
- bench/results/<label>/<scene>.jpg: half-size contact sheets (committed).
- bench/out/sheets/<scene>.png: full-size contact sheets; bench/out/clips/<scene>.mp4: side-by-side
  clips; bench/out/review.html shows both per scene (all local). Each row puts ours, rendered at
  the size that reference app draws its image on the tablet (nearest neighbour, like the M1
  renderer), next to that app's screenshot or recording.

Metrics, all on the display path's output unless marked °C:
- temporal_noise (flat): median per-pixel temporal std, after removing each pixel's quadratic trend
  over the dump (the camera drifts a few counts in 8 s after a NUC); 8-bit display levels and mK.
  Also in mK: the part all pixels share (common_mk) and the noise's lag-1 correlation (rho1).
- stripes (flat): std of column means and of row means of the time-averaged frame after removing a
  cubic 2D polynomial; levels and mK.
- flicker (static scenes): std of the frame's mean display level over time, quadratic trend removed.
- edges (bench/<scene>/roi.json "edges"): slanted-edge profile per frame (edge_frame): its width
  at half slope in pixels (sharpness) and its largest dip in % of the step (halos); medians over
  frames. Edges should be straight, and ideally tilted a few degrees off the pixel grid.
- detail (roi.json "detail"): RMS of the time-averaged frame minus its Gaussian blur (sigma 2 px)
  inside the ROI; levels and mK. Fine structure a stage must keep (key gaps on `keyboard`).

    tools/py/.venv/bin/python tools/py/bench.py [--no-clips] [--rois] [SCENE ...]
"""

import argparse
import datetime
import json
import pathlib
import subprocess

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parents[2]
BENCH = ROOT / "bench"
OUT = BENCH / "out"
HARNESS_BUILD = ROOT / "build" / "harness"
W, H = 256, 192
APPS = {"hti": "Hti Image", "xtherm": "Xtherm", "inficamplus": "InfiCamPlus"}
STATIC = ("flat", "room", "keyboard", "night")  # nothing moving (night was handheld but held steady)
ESF_HALF, PLATEAU, BIN = 9.0, 6.0, 0.25  # edge window, plateau start, bin width (pixels)


# ---- harness -------------------------------------------------------------------------------------

def run_harness(scenes):
    if not (HARNESS_BUILD / "CMakeCache.txt").exists():
        subprocess.run(["cmake", "-S", ROOT / "tools/harness", "-B", HARNESS_BUILD], check=True)
    subprocess.run(["cmake", "--build", HARNESS_BUILD], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([HARNESS_BUILD / "harness", "bench", *scenes], check=True)


def load(scene, stage="baseline"):
    d = OUT / scene
    info = json.loads((d / "info.json").read_text())
    n = info["frames"]
    disp = np.fromfile(d / f"{stage}.f32", dtype="<f4").reshape(n, H, W)
    temp = np.fromfile(d / f"{stage}_c.f32", dtype="<f4").reshape(n, H, W)
    return info, disp, temp


def label():
    """The last commit that changed the display path or the metrics (docs-only commits don't fork
    results), suffixed -dirty while those paths have uncommitted changes."""
    paths = ["native/core", "tools/harness", "palettes", "tools/py/bench.py"]  # what the harness runs
    git = lambda *a: subprocess.run(["git", "-C", ROOT, *a], capture_output=True, text=True, check=True).stdout.strip()
    commit = git("log", "-1", "--format=%h", "--", *paths)
    dirty = git("status", "--porcelain", "--", *paths) != ""
    return commit, dirty, commit + ("-dirty" if dirty else "")


# ---- metrics -------------------------------------------------------------------------------------

def detrend(x):
    """x: (N, ...) time series; removes each series' least-squares quadratic in time."""
    n = x.shape[0]
    t = np.linspace(-1, 1, n)
    q, _ = np.linalg.qr(np.stack([np.ones(n), t, t * t], axis=1))
    flat = x.reshape(n, -1).astype(np.float64)
    return (flat - q @ (q.T @ flat)).reshape(x.shape)


def temporal_noise(x):
    """x: (N, H, W). Median per-pixel std over time after the quadratic detrend, the std of the
    frame-mean residual (the part every pixel shares: a slow wander, M3), and the pooled lag-1
    autocorrelation of the per-pixel residuals (the camera's own filter gives ~0.7)."""
    good = np.isfinite(x).all(axis=0)
    r = detrend(np.where(np.isfinite(x), x, 0.0))[:, good]
    std = np.sqrt((r * r).sum(axis=0) / (x.shape[0] - 3))
    common = np.sqrt((r.mean(axis=1) ** 2).sum() / (x.shape[0] - 3))
    rho1 = (r[:-1] * r[1:]).sum() / np.sqrt((r[:-1] ** 2).sum() * (r[1:] ** 2).sum())
    return float(np.median(std)), float(common), float(rho1)


def remove_poly2d(img, order=3):
    yy, xx = np.mgrid[0:img.shape[0], 0:img.shape[1]]
    u, v = xx / (img.shape[1] - 1) * 2 - 1, yy / (img.shape[0] - 1) * 2 - 1
    terms = [u ** i * v ** j for i in range(order + 1) for j in range(order + 1 - i)]
    good = np.isfinite(img)
    a = np.stack([t[good] for t in terms], axis=1)
    coef, *_ = np.linalg.lstsq(a, img[good], rcond=None)
    fit = sum(c * t for c, t in zip(coef, terms))
    return img - fit


def stripes(mean_frame):
    r = remove_poly2d(mean_frame)
    return float(np.nanstd(np.nanmean(r, axis=0))), float(np.nanstd(np.nanmean(r, axis=1)))


def flicker(disp):
    m = disp.reshape(disp.shape[0], -1).mean(axis=1) * 255
    return float(np.std(detrend(m), ddof=3))


def edge_frame(img, roi):
    """Slanted-edge profile of one frame. Returns (fwhm_px, halo_pct) or None.

    Fits a line to the edge's per-line midpoint crossings, bins every pixel by its distance from that
    line (0.25 px) and smooths the profile over 1 px, which averages out row and column offsets that
    alias into the sub-pixel bins. Sharpness is the width at half height of the profile's derivative
    (the line spread function): real edges here have long tails (skin gets warmer away from the
    silhouette), which a 10-90 % rise would mostly measure. The halo is the profile's largest dip
    moving from the dark side to the bright side, in % of the step: a blurred edge only ever rises,
    a halo makes it bump back.
    """
    a = img[roi["y"]:roi["y"] + roi["h"], roi["x"]:roi["x"] + roi["w"]].astype(np.float64)
    if roi["profile"] == "y":
        a = a.T  # rows run along the edge, columns across it
    lo, hi = np.percentile(a, 10), np.percentile(a, 90)
    if hi - lo <= 0:
        return None
    mid = (lo + hi) / 2
    rows, cols = [], []
    for i, line in enumerate(a):
        d = line - mid
        k = np.nonzero(np.sign(d[:-1]) != np.sign(d[1:]))[0]
        if len(k) == 1:
            k = k[0]
            rows.append(i)
            cols.append(k + d[k] / (d[k] - d[k + 1]))
    if len(rows) < 0.6 * a.shape[0]:
        return None
    rows, cols = np.array(rows, float), np.array(cols)
    p = np.polyfit(rows, cols, 1)
    keep = np.abs(cols - np.polyval(p, rows)) < 1.5
    if keep.sum() < 0.5 * a.shape[0]:
        return None
    p = np.polyfit(rows[keep], cols[keep], 1)
    rr, cc = np.mgrid[0:a.shape[0], 0:a.shape[1]]
    dist = (cc - np.polyval(p, rr)) / np.hypot(1.0, p[0])
    if a[dist > 3].mean() < a[dist < -3].mean():
        dist = -dist  # positive distance = the bright side
    edges = np.arange(-ESF_HALF, ESF_HALF + BIN / 2, BIN)
    centers = (edges[:-1] + edges[1:]) / 2
    idx = np.digitize(dist.ravel(), edges) - 1
    ok = (idx >= 0) & (idx < len(centers))
    counts = np.bincount(idx[ok], minlength=len(centers))
    sums = np.bincount(idx[ok], weights=a.ravel()[ok], minlength=len(centers))
    have = counts > 0
    if have.sum() < 0.5 * len(centers):
        return None
    esf = np.interp(centers, centers[have], sums[have] / np.maximum(counts[have], 1))
    box = np.ones(int(round(1 / BIN))) * BIN
    esf, centers = np.convolve(esf, box, "valid"), np.convolve(centers, box, "valid")
    low, high = np.median(esf[centers <= -PLATEAU]), np.median(esf[centers >= PLATEAU])
    step = high - low
    if step < 0.5 * (hi - lo):
        return None
    lsf, at = np.diff(esf) / BIN, (centers[:-1] + centers[1:]) / 2
    near = np.nonzero(np.abs(at) < 3)[0]
    k = near[np.argmax(lsf[near])]
    half = lsf[k] / 2
    i, j = k, k
    while i > 0 and lsf[i] > half:
        i -= 1
    while j < len(lsf) - 1 and lsf[j] > half:
        j += 1
    if lsf[i] > half or lsf[j] > half:
        return None
    left = at[i] + (half - lsf[i]) * (at[i + 1] - at[i]) / (lsf[i + 1] - lsf[i])
    right = at[j - 1] + (half - lsf[j - 1]) * (at[j] - at[j - 1]) / (lsf[j] - lsf[j - 1])
    dip = float(np.max(np.maximum.accumulate(esf) - esf))
    return float(right - left), float(100 * dip / step)


def edge_metrics(disp, roi):
    res = [r for r in (edge_frame(f, roi) for f in disp) if r is not None]
    if not res:
        return {"frames": 0}
    fwhm, halo = np.array(res).T
    return {"fwhm_px": round(float(np.median(fwhm)), 3), "halo_pct": round(float(np.median(halo)), 2),
            "frames": len(res)}


def detail(mean_frame, roi):
    blur = cv2.GaussianBlur(mean_frame.astype(np.float64), (0, 0), 2.0, borderType=cv2.BORDER_REFLECT)
    s = (slice(roi["y"], roi["y"] + roi["h"]), slice(roi["x"], roi["x"] + roi["w"]))
    return float(np.sqrt(np.nanmean((mean_frame - blur)[s] ** 2)))


def scene_metrics(scene, disp, temp):
    m = {}
    if scene == "flat":
        levels, _, _ = temporal_noise(disp * 255.0)
        c, common, rho1 = temporal_noise(temp)
        m["temporal_noise"] = {"display_levels": round(levels, 4), "mk": round(1000 * c, 2),
                               "common_mk": round(1000 * common, 2), "rho1": round(rho1, 3)}
        cl, rl = stripes(disp.mean(axis=0) * 255)
        cm, rm = stripes(np.mean(temp, axis=0).astype(np.float64))
        m["stripes"] = {"col_levels": round(cl, 4), "row_levels": round(rl, 4),
                        "col_mk": round(1000 * cm, 2), "row_mk": round(1000 * rm, 2)}
    if scene in STATIC:
        m["flicker_levels"] = round(flicker(disp), 4)
    roi_file = BENCH / scene / "roi.json"
    if roi_file.exists():
        rois = json.loads(roi_file.read_text())
        if rois.get("edges"):
            m["edges"] = {r["name"]: edge_metrics(disp, r) for r in rois["edges"]}
        if rois.get("detail"):
            md, mt = disp.mean(axis=0) * 255, np.mean(temp, axis=0).astype(np.float64)
            m["detail"] = {r["name"]: {"levels": round(detail(md, r), 4), "mk": round(1000 * detail(mt, r), 2)}
                           for r in rois["detail"]}
    return m


# ---- contact sheets and clips --------------------------------------------------------------------

def font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def tag(im, text, size):
    d = ImageDraw.Draw(im)
    f = font(size)
    box = d.textbbox((0, 0), text, font=f)
    pad = size // 3
    d.rectangle((0, 0, box[2] + 2 * pad, box[3] + 2 * pad), fill=(0, 0, 0))
    d.text((pad, pad), text, fill=(255, 255, 0), font=f)
    return im


def ours_at(frame, size):
    g = np.round(np.clip(frame, 0, 1) * 255).astype(np.uint8)  # the shader's float -> unorm8
    return Image.fromarray(g).resize(size, Image.NEAREST).convert("RGB")


def refs(scene):
    f = BENCH / scene / "refs.json"
    return json.loads(f.read_text()) if f.exists() else {}


def contact_sheet(scene, disp, stage_name, results_dir):
    frame = disp[disp.shape[0] // 2]
    rows = []
    for app, info in refs(scene).items():
        crop = BENCH / scene / f"{app}_crop.png"
        if not crop.exists():
            continue
        theirs = Image.open(crop).convert("RGB")
        rows.append((tag(ours_at(frame, theirs.size), f"ThermalView {stage_name}", 40),
                     tag(theirs, APPS.get(app, app), 40)))
    if not rows:
        return None
    gap = 16
    width = max(a.width + gap + b.width for a, b in rows)
    height = sum(a.height for a, _ in rows) + gap * (len(rows) - 1)
    sheet = Image.new("RGB", (width, height), (40, 40, 40))
    y = 0
    for a, b in rows:
        sheet.paste(a, (0, y))
        sheet.paste(b, (a.width + gap, y))
        y += a.height + gap
    (OUT / "sheets").mkdir(parents=True, exist_ok=True)
    full = OUT / "sheets" / f"{scene}.png"
    sheet.save(full)
    results_dir.mkdir(parents=True, exist_ok=True)
    sheet.resize((width // 2, height // 2), Image.LANCZOS).save(results_dir / f"{scene}.jpg", quality=90)
    return full


def even(v):
    return int(v) // 2 * 2


def clip(scene, disp, stage_name):
    """Rows of [ours | app] at half the app's on-screen size; ours is the dump (8 s at 25 fps), the
    app's is seconds 1-9 of its recording. The two weren't recorded at the same moment."""
    rows = [(app, r) for app, r in refs(scene).items() if (BENCH / scene / f"{app}.mp4").exists()]
    if not rows:
        return None
    (OUT / "clips").mkdir(parents=True, exist_ok=True)
    raw = OUT / "clips" / f"{scene}_ours.gray16"
    np.round(np.clip(disp, 0, 1) * 65535).astype("<u2").tofile(raw)
    seconds = disp.shape[0] / 25
    inputs = ["-f", "rawvideo", "-pix_fmt", "gray16le", "-s", f"{W}x{H}", "-r", "25", "-i", str(raw)]
    graph = [f"[0:v]split={len(rows)}" + "".join(f"[o{i}]" for i in range(len(rows)))]
    sizes = []
    for i, (app, r) in enumerate(rows):
        w, h = (r["h"], r["w"]) if r["rotation"] else (r["w"], r["h"])
        w2, h2 = even(w / 2), even(h / 2)
        sizes.append((w2, h2))
        inputs += ["-ss", "1", "-t", f"{seconds:.2f}", "-i", str(BENCH / scene / f"{app}.mp4")]
        turn = {90: ",transpose=2", -90: ",transpose=1"}.get(r["rotation"], "")
        graph.append(f"[o{i}]scale={w2}:{h2}:flags=neighbor,format=yuv420p[a{i}]")
        graph.append(f"[{i + 1}:v]crop={r['w']}:{r['h']}:{r['x']}:{r['y']}{turn},fps=25,"
                     f"scale={w2}:{h2}:flags=area,format=yuv420p[b{i}]")
    width = max(2 * w + 8 for w, _ in sizes)
    for i, (w, h) in enumerate(sizes):
        graph.append(f"[a{i}][b{i}]hstack,pad={width}:{h + (8 if i < len(rows) - 1 else 0)}:0:0:color=0x282828[r{i}]")
    graph.append("".join(f"[r{i}]" for i in range(len(rows))) + f"vstack={len(rows)}[stack]"
                 if len(rows) > 1 else "[r0]null[stack]")
    # Labels: a transparent overlay drawn with Pillow (ffmpeg's drawtext needs freetype).
    total_h = sum(h for _, h in sizes) + 8 * (len(rows) - 1)
    labels = Image.new("RGBA", (width, total_h), (0, 0, 0, 0))
    d, f = ImageDraw.Draw(labels), font(24)
    y = 0
    for (app, _), (w, h) in zip(rows, sizes):
        for x, text in ((0, f"ThermalView {stage_name}"), (w + 8, APPS.get(app, app))):
            box = d.textbbox((0, 0), text, font=f)
            d.rectangle((x, y, x + box[2] + 16, y + box[3] + 16), fill=(0, 0, 0, 255))
            d.text((x + 8, y + 8), text, fill=(255, 255, 0, 255), font=f)
        y += h + 8
    label_png = OUT / "clips" / f"{scene}_labels.png"
    labels.save(label_png)
    inputs += ["-i", str(label_png)]
    graph.append(f"[stack][{len(rows) + 1}:v]overlay=0:0,format=yuv420p[v]")
    out = OUT / "clips" / f"{scene}.mp4"
    subprocess.run(["ffmpeg", "-v", "error", "-y", *inputs, "-filter_complex", ";".join(graph), "-map", "[v]",
                    "-t", f"{seconds:.2f}", "-r", "25", "-c:v", "libx264", "-crf", "18", "-preset", "medium",
                    str(out)], check=True)
    raw.unlink()
    return out


def draw_rois(scene, disp):
    """Debug: ROI outlines on the middle frame, 4x (bench/out/rois/<scene>.png)."""
    roi_file = BENCH / scene / "roi.json"
    if not roi_file.exists():
        return
    rois = json.loads(roi_file.read_text())
    s = 4
    im = ours_at(disp[disp.shape[0] // 2], (W * s, H * s))
    d = ImageDraw.Draw(im)
    for kind, color in (("edges", (255, 0, 0)), ("detail", (0, 160, 255))):
        for r in rois.get(kind, []):
            d.rectangle((r["x"] * s, r["y"] * s, (r["x"] + r["w"]) * s - 1, (r["y"] + r["h"]) * s - 1),
                        outline=color, width=2)
            d.text((r["x"] * s + 3, r["y"] * s + 3), r["name"], fill=color, font=font(20))
    (OUT / "rois").mkdir(parents=True, exist_ok=True)
    im.save(OUT / "rois" / f"{scene}.png")


def review_page(name, scenes, results):
    """bench/out/review.html: per scene, the clip, then the full-size sheet (click to open it)."""
    import html
    parts = [f"<!doctype html><meta charset=utf-8><title>Bench {html.escape(name)}</title>",
             "<style>body{background:#1e1e1e;color:#ddd;font:15px -apple-system,sans-serif;margin:16px}"
             "video,img{width:100%;height:auto;display:block;margin:8px 0 24px}"
             "code{color:#fc6}h2{margin-top:40px}</style>",
             f"<h1>Benchmark {html.escape(name)}</h1><p>Each row: ThermalView at that app's on-screen size, then the app "
             "(Hti Image, Xtherm, InfiCamPlus). The clips weren't recorded at the same moment as ours.</p>"]
    for scene in scenes:
        parts.append(f"<h2>{html.escape(scene)}</h2><p><code>{html.escape(json.dumps(results['scenes'].get(scene, {})))}</code></p>")
        if (OUT / "clips" / f"{scene}.mp4").exists():
            parts.append(f'<video src="clips/{scene}.mp4" controls loop muted playsinline></video>')
        if (OUT / "sheets" / f"{scene}.png").exists():
            parts.append(f'<a href="sheets/{scene}.png"><img src="sheets/{scene}.png" loading="lazy"></a>')
    (OUT / "review.html").write_text("\n".join(parts) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("scenes", nargs="*")
    ap.add_argument("--no-clips", action="store_true", help="skip the side-by-side clips")
    ap.add_argument("--rois", action="store_true", help="also draw each scene's ROIs")
    args = ap.parse_args()
    scenes = args.scenes or sorted(p.name for p in BENCH.iterdir() if (p / "thermalview.raw").exists())
    run_harness(scenes)
    commit, dirty, name = label()
    results_dir = BENCH / "results" / name
    results_file = BENCH / "results" / f"{name}.json"
    # A run over some scenes updates just those in this label's results.
    results = json.loads(results_file.read_text()) if results_file.exists() else {"scenes": {}}
    results.update({"label": name, "commit": commit, "dirty": dirty,
                    "date": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
                    "stages": ["baseline"]})
    for scene in scenes:
        info, disp, temp = load(scene)
        # The environment the mK figures were computed with (the camera's user area, as-is).
        results["scenes"][scene] = {"environment": info["environment"]} | scene_metrics(scene, disp, temp)
        if args.rois:
            draw_rois(scene, disp)
        contact_sheet(scene, disp, "baseline", results_dir)
        if not args.no_clips:
            clip(scene, disp, "baseline")
        print(scene, json.dumps(results["scenes"][scene]))
    results = {k: results[k] for k in ("label", "commit", "dirty", "date", "stages")} | {
        "scenes": dict(sorted(results["scenes"].items()))}
    results_file.parent.mkdir(parents=True, exist_ok=True)
    results_file.write_text(json.dumps(results, indent=2) + "\n")
    review_page(name, scenes, results)
    print("wrote", results_file.relative_to(ROOT), "and", (OUT / "review.html").relative_to(ROOT))


if __name__ == "__main__":
    main()
