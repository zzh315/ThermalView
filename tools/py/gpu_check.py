#!/usr/bin/env python3
"""M5's GPU-vs-CPU check (docs/PLAN.md M5): what the tablet's GPU drew against native/core's CPU
reference for the same frame.

With the app showing a frame (a replay, or the camera), this asks it over adb to read back its next
drawn frame (`--ez readback true`), pulls the readback (the drawn view as PPM, the intensity and
over-range mask it came from, and how it was drawn), renders the same intensity with `harness
render` at the same size, upscaler, palette, mirroring and turn, and compares them per channel:

    tools/py/.venv/bin/python tools/py/gpu_check.py [--adb ADB] [--tolerance 2]

Passes when no channel differs by more than --tolerance levels (the plan starts at 2/255), except
at the edges of the masks (over range; a locked range's marks): there the filtered mask sits at the
0.5 threshold, and the GPU's 8-bit filtering and the CPU's float can fall either side of it, so a
pixel may take the mask's color on one and the palette's on the other. Those flips are counted and
reported. Writes build/gpu_check/: gpu.png, cpu.png and diff.png (differences x32).
"""

import argparse
import json
import pathlib
import subprocess
import time

import numpy as np
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "build" / "harness" / "harness"
REMOTE = "/sdcard/Android/data/dev.thermalview/files/readback"
OUT = ROOT / "build" / "gpu_check"


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--adb", default="adb", help="adb command (e.g. the wireless wrapper)")
    ap.add_argument("--tolerance", type=int, default=2)
    args = ap.parse_args()
    adb = lambda *a: subprocess.run([args.adb, *a], check=True, capture_output=True, text=True).stdout
    before = set(adb("shell", f"ls {REMOTE} 2>/dev/null || true").split())
    adb("shell", "am", "start", "-n", "dev.thermalview/.MainActivity", "--ez", "readback", "true")
    for _ in range(20):
        time.sleep(0.5)
        new = sorted(n for n in set(adb("shell", f"ls {REMOTE} 2>/dev/null || true").split()) - before
                     if n.endswith(".json"))
        if new:
            break
    else:
        raise SystemExit("no readback appeared: is the app in front, showing a frame?")
    stem = new[-1][:-len(".json")]
    OUT.mkdir(parents=True, exist_ok=True)
    time.sleep(0.5)  # the PPM is written before the sidecar, but give the filesystem a moment
    for suffix in (".json", ".ppm", ".f32", "_clip.u8"):
        adb("pull", f"{REMOTE}/{stem}{suffix}", str(OUT / f"readback{suffix}"))
    info = json.loads((OUT / "readback.json").read_text())
    if info.get("locked"):  # M6's range lock with a palette that marks it: the marks too
        adb("pull", f"{REMOTE}/{stem}_outside.u8", str(OUT / "readback_outside.u8"))
    cmd = [HARNESS, "render", OUT / "readback.f32", "--clip", OUT / "readback_clip.u8",
           "--size", f"{info['width']}x{info['height']}", "--kernel", info["upscaler"], "--out", OUT / "cpu.ppm"]
    if "rect" in info:  # zoom and pan: the camera pixels the view showed
        cmd += ["--rect", ",".join(f"{v:.4f}" for v in info["rect"])]
    if info["upscaler"] == "bspline":
        cmd.append("--clamp")  # the shader always clamps its B-spline
    if info["palette"] != "gray":
        cmd += ["--palette", ROOT / "palettes" / f"{info['palette']}.json"]
    if info.get("locked"):
        cmd += ["--outside", OUT / "readback_outside.u8"]
    box = info.get("box", [0, 0, 0, 0])
    if box[2] > box[0]:  # M6's box: the image outside it dimmed
        cmd += ["--box", f"{box[0]:.0f},{box[1]:.0f},{box[2] - box[0]:.0f},{box[3] - box[1]:.0f}", "--dim", f"{info.get('dim', 0.5)}"]
    if info["mirror_x"]:
        cmd.append("--mirror-x")
    if info["mirror_y"]:
        cmd.append("--mirror-y")
    rot = int(info.get("rot", 0))
    if rot:  # M6's orientation: the image turned in the view
        cmd += ["--rotate", str(rot)]
    if float(info.get("sharpen", 0)) > 0:  # M7's edge sharpening
        cmd += ["--sharpen", str(info["sharpen"])]
    subprocess.run(["cmake", "--build", ROOT / "build" / "harness"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(cmd, check=True)
    gpu = np.asarray(Image.open(OUT / "readback.ppm").convert("RGB")).astype(int)
    cpu = np.asarray(Image.open(OUT / "cpu.ppm").convert("RGB")).astype(int)
    diff = np.abs(gpu - cpu)
    Image.fromarray(gpu.astype(np.uint8)).save(OUT / "gpu.png")
    Image.fromarray(cpu.astype(np.uint8)).save(OUT / "cpu.png")
    Image.fromarray(np.clip(diff * 32, 0, 255).astype(np.uint8)).save(OUT / "diff.png")
    over = float((diff.max(axis=2) > args.tolerance).mean() * 100)
    print(f"{info['width']}x{info['height']} {info['upscaler']} {info['palette']}: max difference {diff.max()} "
          f"levels, 99.9th percentile {np.percentile(diff, 99.9):.1f}, {over:.4f}% of pixels over {args.tolerance}")
    # The masks' edges (camera pixels next to a change in a mask), mapped to the view's pixels.
    masks = [np.fromfile(OUT / "readback_clip.u8", dtype=np.uint8).reshape(192, 256) > 127]
    if info.get("locked"):
        marks = np.fromfile(OUT / "readback_outside.u8", dtype=np.uint8).reshape(192, 256)
        masks += [marks == 1, marks == 2]
    edge = np.zeros((192, 256), bool)
    for m in masks:
        h = m[:, 1:] != m[:, :-1]
        v = m[1:, :] != m[:-1, :]
        edge[:, 1:] |= h
        edge[:, :-1] |= h
        edge[1:, :] |= v
        edge[:-1, :] |= v
    # Each view pixel's camera pixel: unmirrored, then turned back (renderer.cpp's vertex shader).
    rx, ry, rw, rh = info.get("rect", [0, 0, 256, 192])
    height, width = diff.shape[:2]
    ys, xs = np.mgrid[0:height, 0:width]
    if info["mirror_x"]:
        xs = width - 1 - xs
    if info["mirror_y"]:
        ys = height - 1 - ys
    s, t = (xs + 0.5) / width, (ys + 0.5) / height
    a, b = {0: (s, t), 1: (t, 1 - s), 2: (1 - s, 1 - t), 3: (1 - t, s)}[rot]
    cx = np.clip((rx + a * rw).astype(int), 0, 255)
    cy = np.clip((ry + b * rh).astype(int), 0, 191)
    at_edge = edge[cy, cx]
    bad = diff.max(axis=2) > args.tolerance
    flips = int((bad & at_edge).sum())
    if flips:
        print(f"{flips} pixels at mask edges take the mask's color on one side and the palette's on the other")
    print("PASS" if not (bad & ~at_edge).any() else "FAIL", "(images in", OUT.relative_to(ROOT), ")")


if __name__ == "__main__":
    main()
