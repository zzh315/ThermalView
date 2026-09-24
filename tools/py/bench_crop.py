#!/usr/bin/env python3
"""Crop each reference app's thermal image out of its bench screenshot (docs/PLAN.md M3).

Each app draws its image at a fixed place (measured in M0; InfiCamPlus sometimes draws a
portrait layout, detected from its screenshot). A portrait image is turned back to landscape,
in the direction that best matches our own capture of the scene. Detecting the area from the
screen recording's flicker was tried and rejected: the video codec makes static UI flicker too.
Writes bench/<scene>/<app>_crop.png (full size, local), <app>.png (half size, for git) and
refs.json with each rectangle.

    tools/py/.venv/bin/python tools/py/bench_crop.py [bench/<scene> ...]
"""

import argparse
import json
import pathlib

import numpy as np
from PIL import Image

APPS = ("hti", "xtherm", "inficamplus")
# (x, y, w, h) on the tablet's 2560x1600 screen (PRIOR_ART.md compatibility matrix; M3 check).
RECTS = {"hti": (290, 60, 1980, 1508), "xtherm": (120, 60, 2010, 1508),
         "inficamplus": (254, 60, 2053, 1540), "inficamplus_portrait": (700, 60, 1160, 1540)}


def inficam_portrait(shot):
    """InfiCamPlus's portrait layout leaves the band left of x=700 black and flat."""
    band = np.asarray(Image.open(shot).convert("L").crop((300, 200, 680, 1400)), dtype=np.float64)
    return band.mean() < 20 and band.std() < 8


def our_frame(scene):
    raw = scene / "thermalview.raw"
    if not raw.exists():
        return None
    f = np.fromfile(raw, dtype="<u2", count=256 * 196).reshape(196, 256)[:192].astype(np.float64)
    lo, hi = np.percentile(f, [0.5, 99.5])
    return np.clip((f - lo) / max(hi - lo, 1), 0, 1)


def similarity(img, ref):
    """Correlation of two gray images after resizing img to ref's shape."""
    a = np.asarray(img.convert("L").resize((ref.shape[1], ref.shape[0])), dtype=np.float64) / 255
    a, b = a - a.mean(), ref - ref.mean()
    return float((a * b).sum() / np.sqrt((a * a).sum() * (b * b).sum() + 1e-12))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("scenes", nargs="*")
    args = ap.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2] / "bench"
    scenes = [pathlib.Path(s) for s in args.scenes] or sorted(p for p in root.iterdir() if (p / "thermalview.json").exists())
    for scene in scenes:
        refs, ours = {}, our_frame(scene)
        for app in APPS:
            shot = scene / f"{app}_full.png"
            if not shot.exists():
                continue
            key = "inficamplus_portrait" if app == "inficamplus" and inficam_portrait(shot) else app
            x, y, w, h = RECTS[key]
            crop = Image.open(shot).convert("RGB").crop((x, y, x + w, y + h))
            rotation = 0
            if h > w:  # portrait: turn back to landscape, whichever way matches our capture
                cands = {r: crop.rotate(r, expand=True) for r in (90, -90)}
                rotation = max(cands, key=lambda r: similarity(cands[r], ours)) if ours is not None else 90
                crop = cands[rotation]
            crop.save(scene / f"{app}_crop.png")
            crop.resize((crop.width // 2, crop.height // 2), Image.LANCZOS).save(scene / f"{app}.png")
            refs[app] = {"x": int(x), "y": int(y), "w": int(w), "h": int(h), "rotation": rotation,
                         "match": round(similarity(crop, ours), 3) if ours is not None else None}
            print(f"{scene.name:9s} {app:12s} rect {x},{y} {w}x{h}  rotation {rotation:+d}  match {refs[app]['match']}")
        (scene / "refs.json").write_text(json.dumps(refs, indent=2) + "\n")


if __name__ == "__main__":
    main()
