#!/usr/bin/env python3
"""Black-box metrics for the reference apps' benchmark captures (docs/PRIOR_ART.md pass 2 item 1).

Observation only: works from the screenshots and screen recordings in bench/<scene>/, never from
the apps themselves. Ours comes from the harness output (run tools/py/bench.py first) and goes
through the same measurements, so the numbers line up.

- Recordings: each app's image area (bench/<scene>/refs.json) is decoded and averaged down to
  camera resolution (256×192), so levels are 8-bit display levels per camera pixel.
  - flicker: std over time of each frame's offset from the recording's median frame (a trimmed mean
    that leaves out the apps' moving labels), with the quadratic trend removed.
  - noise (flat): median per-pixel temporal std after the quadratic detrend. The recordings are
    H.264, which smooths fine noise, so treat this as a lower bound.
  - flicker_steady leaves out frames more than 10 levels off the median frame (an event, such as a
    hot car crossing the view), and largest_jump is the biggest frame-to-frame change of the offset.
  - range: the 1st and 99th percentiles of the display levels (a washed-out image uses less range).
  - freezes: gaps of more than 0.5 s between recorded frames (screenrecord only writes a frame
    when the screen changes, so a shutter cycle shows up as a gap).
- Hand screenshots: an edge profile around the whole hand silhouette (every pixel binned by its
  distance to the Otsu boundary), in camera pixels, on each app's screenshot and on ours drawn
  bicubic at that app's size: its half-slope width (sharpness) and largest dip in % of the step
  (halo), as in bench.py. The hand sat at a different distance in each capture, which changes
  its edges, so compare these with care.
- Keyboard (camera and laptop still; every app aligned to ours within 0.12 px): the screen edge's
  slanted-edge width and halo (bench.py's ROI) with both images averaged back from that app's
  on-screen size, and the key-gap detail on the recording's median frame.

    tools/py/.venv/bin/python tools/py/bench_refs.py   ->  bench/results/refs.json
"""

import json
import pathlib
import subprocess

import cv2
import numpy as np
from PIL import Image

import bench

ROOT = bench.ROOT
BENCH = bench.BENCH
W, H = bench.W, bench.H
APPS = bench.APPS


def decode(scene, app, r):
    """The app's recording, cropped (and turned back to landscape), averaged to 256×192 gray."""
    turn = {90: ",transpose=2", -90: ",transpose=1"}.get(r["rotation"], "")
    # 16-bit output keeps the area average's sub-level precision; passthrough keeps only the frames
    # screenrecord actually wrote (it records on change, at a variable rate).
    vf = f"crop={r['w']}:{r['h']}:{r['x']}:{r['y']}{turn},scale={W}:{H}:flags=area,format=gray16le"
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", str(BENCH / scene / f"{app}.mp4"), "-vf", vf,
                          "-fps_mode", "passthrough", "-f", "rawvideo", "-"], capture_output=True, check=True).stdout
    frames = np.frombuffer(raw, dtype="<u2").reshape(-1, H, W).astype(np.float64) / 257.0
    pts = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
                          "frame=best_effort_timestamp_time", "-of", "csv=p=0",
                          str(BENCH / scene / f"{app}.mp4")], capture_output=True, text=True, check=True).stdout
    t = np.array([float(x) for x in pts.split() if x.strip() not in ("", "N/A")])
    return frames, t


def frame_offsets(frames):
    """Each frame's global offset from the median frame: the mean of the central 80 % of its pixel
    differences, which leaves out the apps' moving labels and markers."""
    d = (frames - np.median(frames, axis=0)).reshape(frames.shape[0], -1)
    lo, hi = np.percentile(d, [10, 90], axis=1, keepdims=True)
    keep = (d >= lo) & (d <= hi)
    return (d * keep).sum(axis=1) / keep.sum(axis=1)


def flicker(frames):
    return float(np.std(bench.detrend(frame_offsets(frames)), ddof=3))


def temporal_metrics(frames, t=None):
    off = frame_offsets(frames)
    jumps = np.abs(np.diff(off))
    # Frames more than 10 levels off the median frame belong to an event (something hot crossing
    # the view makes a min/max auto range pump); flicker_steady leaves them out.
    steady = np.abs(off - np.median(off)) < 10
    m = {"flicker_levels": round(flicker(frames), 3),
         "flicker_steady_levels": round(float(np.std(bench.detrend(off[steady]), ddof=3)), 3) if steady.sum() > 10 else None,
         "largest_jump_levels": round(float(jumps.max()), 2) if len(jumps) else None,
         "range_p1_p99": [round(float(np.percentile(frames, 1)), 1), round(float(np.percentile(frames, 99)), 1)]}
    if t is not None and len(t) > 1:
        gaps = np.diff(t)
        m["frames"] = int(frames.shape[0])
        m["freezes"] = [round(float(g), 2) for g in gaps[gaps > 0.5]]
    return m


def silhouette_profile(img, px_per_cam):
    """Edge profile around the bright silhouette of a gray image (float, any scale): distances are
    converted to camera pixels. Returns (fwhm_px, halo_pct) or None."""
    g = img.astype(np.float64)
    g8 = np.clip(g, 0, 255).astype(np.uint8)
    _, mask = cv2.threshold(g8, 0, 1, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, np.ones((5, 5), np.uint8))
    inside = cv2.distanceTransform(mask, cv2.DIST_L2, 5)
    outside = cv2.distanceTransform(1 - mask, cv2.DIST_L2, 5)
    dist = (np.where(mask > 0, inside - 0.5, -(outside - 0.5))) / px_per_cam  # camera px, + = inside
    half, step = 6.0, 0.25
    edges = np.arange(-half, half + step / 2, step)
    centers = (edges[:-1] + edges[1:]) / 2
    idx = np.digitize(dist.ravel(), edges) - 1
    ok = (idx >= 0) & (idx < len(centers))
    counts = np.bincount(idx[ok], minlength=len(centers))
    sums = np.bincount(idx[ok], weights=g.ravel()[ok], minlength=len(centers))
    if (counts == 0).any():
        return None
    esf = sums / counts
    box = np.ones(4) / 4  # 1 camera px
    esf, centers = np.convolve(esf, box, "valid"), np.convolve(centers, box, "valid")
    low, high = np.median(esf[centers <= -4]), np.median(esf[centers >= 4])
    if high - low <= 0:
        return None
    lsf, at = np.diff(esf) / step, (centers[:-1] + centers[1:]) / 2
    near = np.nonzero(np.abs(at) < 2)[0]
    k = near[np.argmax(lsf[near])]
    halfmax = lsf[k] / 2
    i, j = k, k
    while i > 0 and lsf[i] > halfmax:
        i -= 1
    while j < len(lsf) - 1 and lsf[j] > halfmax:
        j += 1
    left = at[i] + (halfmax - lsf[i]) * (at[i + 1] - at[i]) / (lsf[i + 1] - lsf[i])
    right = at[j - 1] + (halfmax - lsf[j - 1]) * (at[j] - at[j - 1]) / (lsf[j] - lsf[j - 1])
    dip = float(np.max(np.maximum.accumulate(esf) - esf))
    return round(float(right - left), 3), round(100 * dip / (high - low), 2)


def main():
    out = {"note": "Black-box metrics of the reference apps' captures (bench_refs.py); 8-bit display levels "
                   "per camera pixel; recordings are H.264, so noise is a lower bound.", "scenes": {}}
    for scene in sorted(p.name for p in BENCH.iterdir() if (p / "thermalview.json").exists() and (p / "refs.json").exists()):
        refs = json.loads((BENCH / scene / "refs.json").read_text())
        res = {}
        info, disp, _ = bench.load(scene)
        ours = disp * 255.0
        res["thermalview"] = temporal_metrics(ours)
        if scene == "flat":
            res["thermalview"]["noise_levels"] = round(bench.temporal_noise(ours)[0], 3)
        for app, r in refs.items():
            if not (BENCH / scene / f"{app}.mp4").exists():
                continue
            frames, t = decode(scene, app, r)
            res[app] = temporal_metrics(frames, t)
            if scene == "flat":
                res[app]["noise_levels"] = round(bench.temporal_noise(frames)[0], 3)
        if scene == "hand":
            frame = disp[disp.shape[0] // 2]
            for app, r in refs.items():
                crop = BENCH / scene / f"{app}_crop.png"
                if not crop.exists():
                    continue
                theirs = Image.open(crop).convert("L")
                scale = theirs.width / W
                mine = np.asarray(bench.ours_at(frame, theirs.size, "bicubic").convert("L"), dtype=np.float64)
                res[app]["hand_edge"] = dict(zip(("fwhm_px", "halo_pct"),
                                                 silhouette_profile(np.asarray(theirs, dtype=np.float64), scale) or (None, None)))
                res.setdefault("thermalview_at_" + app, {})["hand_edge"] = dict(zip(("fwhm_px", "halo_pct"),
                                                                                    silhouette_profile(mine, scale) or (None, None)))
        if scene == "keyboard":
            # The camera and laptop didn't move and every app shows the whole sensor (aligned to within
            # 0.12 px), so the ROIs apply as they are. Edge: each image at that app's on-screen size,
            # averaged back to camera pixels (ours drawn bicubic), so upscaler blur counts for both.
            # Detail: the recording's median frame (noise averages out) vs our time-averaged frame.
            rois = json.loads((BENCH / scene / "roi.json").read_text())
            edge, keys = rois["edges"][0], rois["detail"][0]
            frame = disp[disp.shape[0] // 2]
            res["thermalview"]["keys_detail_levels"] = round(bench.detail(disp.mean(axis=0) * 255.0, keys), 3)
            for app, r in refs.items():
                crop = BENCH / scene / f"{app}_crop.png"
                if not crop.exists():
                    continue
                theirs = Image.open(crop).convert("L")
                back = lambda im: cv2.resize(np.asarray(im, dtype=np.float64), (W, H), interpolation=cv2.INTER_AREA) / 255
                e = bench.edge_frame(back(theirs), edge)
                res[app]["screen_edge"] = {"fwhm_px": round(e[0], 3), "halo_pct": round(e[1], 2)} if e else None
                e = bench.edge_frame(back(bench.ours_at(frame, theirs.size, "bicubic").convert("L")), edge)
                res.setdefault("thermalview_at_" + app, {})["screen_edge"] = (
                    {"fwhm_px": round(e[0], 3), "halo_pct": round(e[1], 2)} if e else None)
                if (BENCH / scene / f"{app}.mp4").exists():
                    frames, _ = decode(scene, app, r)
                    res[app]["keys_detail_levels"] = round(bench.detail(np.median(frames, axis=0), keys), 3)
        out["scenes"][scene] = res
        print(scene, json.dumps(res))
    (BENCH / "results" / "refs.json").write_text(json.dumps(out, indent=2) + "\n")
    print("wrote bench/results/refs.json")


if __name__ == "__main__":
    main()
